// 摇骰子 / 抛硬币 —— 骰子占满主屏(甩动或点屏触发,翻滚减速定格)。
// 主视图右上小齿轮 → 进本 app 独立设置页(单独一页选 1/2/3 颗骰子 或 硬币,存 NVS);
// 设置页右滑/‹ 返回主视图。骰子全同(≥2 颗)→ 豹子变红;硬币 → 正/反 大字。
#include "app.h"
#include "imu.h"
#include "settings.h"
#include "esp_random.h"
#include <stdio.h>
#include <math.h>

#define DCY        228           // 骰体/硬币竖直中心(主角,基本居中偏下)
#define ROLL_TICKS 20
#define M_COIN     3             // 模式 3 = 硬币

static const struct { int cnt; int8_t cx[6], cy[6]; } FACE[7] = {
    { 0, {0},{0} },
    { 1, { 0},        { 0} },
    { 2, {-1, 1},     {-1, 1} },
    { 3, {-1, 0, 1},  {-1, 0, 1} },
    { 4, {-1, 1,-1, 1},{-1,-1, 1, 1} },
    { 5, {-1, 1, 0,-1, 1},{-1,-1, 0, 1, 1} },
    { 6, {-1, 1,-1, 1,-1, 1},{-1,-1, 0, 0, 1, 1} },
};

static int   s_mode = 1;
static int   s_val[3] = { 1, 1, 1 };
static int   s_diesz, s_pipo, s_pipr;
static int   s_roll, s_next;
static int   s_view;                     // 0=主视图,1=设置页
static float s_ax, s_ay, s_az;
static bool  s_have_last;

static lv_obj_t *g_main, *g_set;         // 主视图容器 / 设置页容器
static lv_obj_t *g_stage, *g_die[3], *g_pips[3], *g_coin, *g_coinlbl, *g_sum, *g_hint;

static const char *mode_label(int m) {
    switch (m) { case 0: return tr(S_1DIE); case 1: return tr(S_2DICE); case 2: return tr(S_3DICE); default: return tr(S_COIN); }
}

static lv_obj_t *mkdot(lv_obj_t *p, int x, int y, int r, uint32_t col) {
    lv_obj_t *d = lv_obj_create(p);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, r * 2, r * 2);
    lv_obj_set_pos(d, x - r, y - r);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(d, lv_color_hex(col), 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_add_flag(d, LV_OBJ_FLAG_EVENT_BUBBLE);
    return d;
}

static void draw_face(int i, int v, uint32_t col) {
    lv_obj_clean(g_pips[i]);
    int c = s_diesz / 2;
    for (int k = 0; k < FACE[v].cnt; k++)
        mkdot(g_pips[i], c + FACE[v].cx[k] * s_pipo, c + FACE[v].cy[k] * s_pipo, s_pipr, col);
}

/* 主视图舞台:按模式建骰子群或硬币(切模式/进入时调) */
static void build_stage(void) {
    lv_obj_clean(g_stage);
    g_die[0] = g_die[1] = g_die[2] = g_coin = g_coinlbl = NULL;

    if (s_mode == M_COIN) {
        int D = 168;
        g_coin = lv_obj_create(g_stage);
        lv_obj_remove_style_all(g_coin);
        lv_obj_set_size(g_coin, D, D);
        lv_obj_set_pos(g_coin, 233 - D / 2, DCY - D / 2);
        lv_obj_set_style_radius(g_coin, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(g_coin, lv_color_hex(0x141418), 0);
        lv_obj_set_style_bg_opa(g_coin, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(g_coin, 3, 0);
        lv_obj_set_style_border_color(g_coin, lv_color_hex(COL_TXT), 0);
        lv_obj_add_flag(g_coin, LV_OBJ_FLAG_EVENT_BUBBLE);
        g_coinlbl = lv_label_create(g_coin);
        lv_obj_set_style_text_font(g_coinlbl, &lv_font_montserrat_40, 0);
        lv_obj_center(g_coinlbl);
        lv_obj_add_flag(g_coinlbl, LV_OBJ_FLAG_EVENT_BUBBLE);
        return;
    }

    int N = s_mode + 1;
    static const int SZ[3] = { 172, 130, 98 };
    static const int GP[3] = { 0, 28, 18 };
    s_diesz = SZ[N - 1];
    s_pipo  = s_diesz * 27 / 100;
    s_pipr  = s_diesz * 9 / 100;
    int total = N * s_diesz + (N - 1) * GP[N - 1];
    int x0 = 233 - total / 2;
    for (int i = 0; i < N; i++) {
        int cx = x0 + s_diesz / 2 + i * (s_diesz + GP[N - 1]);
        g_die[i] = lv_obj_create(g_stage);
        lv_obj_remove_style_all(g_die[i]);
        lv_obj_set_size(g_die[i], s_diesz, s_diesz);
        lv_obj_set_pos(g_die[i], cx - s_diesz / 2, DCY - s_diesz / 2);
        lv_obj_set_style_radius(g_die[i], s_diesz / 5, 0);
        lv_obj_set_style_bg_color(g_die[i], lv_color_hex(0x141418), 0);
        lv_obj_set_style_bg_opa(g_die[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(g_die[i], 2, 0);
        lv_obj_set_style_border_color(g_die[i], lv_color_hex(COL_TXT), 0);
        lv_obj_add_flag(g_die[i], LV_OBJ_FLAG_EVENT_BUBBLE);
        g_pips[i] = lv_obj_create(g_die[i]);
        lv_obj_remove_style_all(g_pips[i]);
        lv_obj_set_size(g_pips[i], s_diesz, s_diesz);
        lv_obj_add_flag(g_pips[i], LV_OBJ_FLAG_EVENT_BUBBLE);
    }
}

static void render(void) {
    if (s_mode == M_COIN) {
        if (!g_coinlbl) return;
        lv_label_set_text(g_coinlbl, tr(s_val[0] ? S_HEADS : S_TAILS));
        lv_label_set_text(g_sum, "");
        lv_label_set_text(g_hint, "");
        return;
    }
    int N = s_mode + 1;
    bool same = (N >= 2);
    for (int i = 1; i < N; i++) if (s_val[i] != s_val[0]) same = false;
    uint32_t col = same ? COL_RED : COL_TXT;
    int sum = 0;
    for (int i = 0; i < N; i++) { draw_face(i, s_val[i], col); sum += s_val[i]; }
    char b[16]; snprintf(b, sizeof b, "%d", sum);
    lv_label_set_text(g_sum, b);
    lv_obj_set_style_text_color(g_sum, lv_color_hex(same ? COL_RED : COL_TXT2), 0);
    lv_label_set_text(g_hint, same ? tr(S_DICE_DOUBLE) : "");
}

static void roll_once(void) {
    if (s_mode == M_COIN) s_val[0] = (int)(esp_random() & 1);
    else for (int i = 0; i < s_mode + 1; i++) s_val[i] = 1 + (int)(esp_random() % 6);
    render();
}

static void start_roll(void) {
    if (s_roll > 0 || s_view != 0) return;
    s_roll = ROLL_TICKS;
    s_next = 0;
    lv_label_set_text(g_hint, "");
}

static void tap_cb(lv_event_t *e) { (void)e; start_roll(); }

/* ---- 独立设置页:4 项列表(1/2/3 颗 / 硬币),当前红高亮,点即选 ---- */
static void set_highlight(lv_obj_t *list, int sel) {
    int n = (int)lv_obj_get_child_count(list);
    for (int i = 0; i < n; i++) {
        lv_obj_t *it = lv_obj_get_child(list, i);
        lv_obj_set_style_bg_color(it, lv_color_hex(i == sel ? 0x2a1216 : 0x16161c), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(it, 0), lv_color_hex(i == sel ? COL_RED : COL_TXT), 0);
    }
}
static void setrow_cb(lv_event_t *e) {
    int m = (int)(intptr_t)lv_event_get_user_data(e);
    s_mode = m;
    settings_set_dice((uint8_t)m);
    settings_save();
    set_highlight(lv_obj_get_parent(lv_event_get_target_obj(e)), m);   // 原地改高亮,不重建
}

static void open_settings(lv_event_t *e) {
    (void)e;
    s_roll = 0;
    lv_obj_clean(g_set);

    lv_obj_t *title = lv_label_create(g_set);
    lv_obj_set_style_text_font(title, UI_FONT_M, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COL_TXT2), 0);
    lv_obj_set_style_text_letter_space(title, 3, 0);
    lv_label_set_text(title, tr(S_DICE_MODE));
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 66);

    lv_obj_t *list = lv_obj_create(g_set);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 300, 320);
    lv_obj_align(list, LV_ALIGN_CENTER, 0, 26);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list, 8, 0);
    lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(list, LV_OBJ_FLAG_EVENT_BUBBLE);
    for (int i = 0; i < 4; i++) {
        lv_obj_t *it = lv_obj_create(list);
        lv_obj_remove_style_all(it);
        lv_obj_set_size(it, 240, 54);
        lv_obj_set_style_radius(it, 14, 0);
        lv_obj_set_style_bg_color(it, lv_color_hex(i == s_mode ? 0x2a1216 : 0x16161c), 0);
        lv_obj_set_style_bg_opa(it, LV_OPA_COVER, 0);
        lv_obj_add_flag(it, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(it, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_event_cb(it, setrow_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *l = lv_label_create(it);
        lv_obj_set_style_text_font(l, UI_FONT_L, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(i == s_mode ? COL_RED : COL_TXT), 0);
        lv_label_set_text(l, mode_label(i));
        lv_obj_center(l);
    }

    s_view = 1;
    lv_obj_add_flag(g_main, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g_set, LV_OBJ_FLAG_HIDDEN);
    launcher_set_title(tr(S_DICE_MODE));
}

// 框架返回:设置页 → 回主视图(消费);主视图 → 返 false 退出 app
static bool dice_back(void) {
    if (s_view == 0) return false;
    s_view = 0;
    lv_obj_add_flag(g_set, LV_OBJ_FLAG_HIDDEN);
    s_val[0] = s_val[1] = s_val[2] = (s_mode == M_COIN) ? 0 : 1;
    build_stage();                       // 模式可能变了,重建主视图舞台
    render();
    lv_obj_remove_flag(g_main, LV_OBJ_FLAG_HIDDEN);
    launcher_set_title(tr_app_name("dice"));
    return true;
}

static void dice_enter(lv_obj_t *parent) {
    s_mode = settings_dice();

    /* ---- 主视图 ---- */
    g_main = lv_obj_create(parent);
    lv_obj_remove_style_all(g_main);
    lv_obj_set_size(g_main, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(g_main, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_main, LV_OBJ_FLAG_EVENT_BUBBLE);

    g_stage = lv_obj_create(g_main);
    lv_obj_remove_style_all(g_stage);
    lv_obj_set_size(g_stage, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(g_stage, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_stage, LV_OBJ_FLAG_EVENT_BUBBLE);

    g_hint = lv_label_create(g_main);
    lv_obj_set_style_text_font(g_hint, UI_FONT_M, 0);
    lv_obj_set_style_text_color(g_hint, lv_color_hex(COL_TXT2), 0);
    lv_obj_align(g_hint, LV_ALIGN_TOP_MID, 0, 92);

    g_sum = lv_label_create(g_main);
    lv_obj_set_style_text_font(g_sum, UI_FONT_SYM, 0);
    lv_obj_set_style_text_color(g_sum, lv_color_hex(COL_TXT2), 0);
    lv_obj_align(g_sum, LV_ALIGN_BOTTOM_MID, 0, -40);

    // 全屏点击区 = 摇(建在舞台之上、齿轮之下)
    lv_obj_t *hit = lv_obj_create(g_main);
    lv_obj_remove_style_all(hit);
    lv_obj_set_size(hit, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(hit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(hit, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(hit, tap_cb, LV_EVENT_CLICKED, NULL);

    // 右上齿轮 → 设置页(圆形描边,与返回键对称)
    lv_obj_t *gear = lv_obj_create(g_main);
    lv_obj_set_size(gear, 46, 46);
    lv_obj_set_style_radius(gear, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(gear, lv_color_hex(0x16161a), 0);
    lv_obj_set_style_bg_opa(gear, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(gear, 1, 0);
    lv_obj_set_style_border_color(gear, lv_color_hex(COL_TXT2), 0);
    lv_obj_set_style_pad_all(gear, 0, 0);
    lv_obj_align(gear, LV_ALIGN_TOP_MID, 100, 40);
    lv_obj_remove_flag(gear, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(gear, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(gear, open_settings, LV_EVENT_CLICKED, NULL);
    lv_obj_t *gl = lv_label_create(gear);
    lv_obj_set_style_text_font(gl, UI_FONT_SYM, 0);
    lv_obj_set_style_text_color(gl, lv_color_hex(COL_TXT), 0);
    lv_label_set_text(gl, LV_SYMBOL_SETTINGS);
    lv_obj_center(gl);

    /* ---- 设置页容器(默认隐藏)---- */
    g_set = lv_obj_create(parent);
    lv_obj_remove_style_all(g_set);
    lv_obj_set_size(g_set, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(g_set, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_set, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(g_set, LV_OBJ_FLAG_HIDDEN);

    s_view = 0;
    build_stage();
    lv_label_set_text(g_hint, tr(imu_init() ? S_DICE_HINT : S_DICE_TAP));
    s_roll = 0; s_have_last = false;
    render();
}

static void dice_tick(void) {
    if (!g_stage || s_view != 0) return;

    float ax, ay, az;
    if (imu_read_accel(&ax, &ay, &az)) {
        if (s_have_last) {
            float jerk = fabsf(ax - s_ax) + fabsf(ay - s_ay) + fabsf(az - s_az);
            if (jerk > 1.1f) start_roll();
        }
        s_ax = ax; s_ay = ay; s_az = az; s_have_last = true;
    }

    if (s_roll > 0) {
        if (--s_next <= 0) { roll_once(); int done = ROLL_TICKS - s_roll; s_next = 1 + done / 4; }
        s_roll--;
        if (s_roll == 0) roll_once();
    }
}

static void dice_exit(void) {
    g_main = g_set = g_stage = g_die[0] = g_die[1] = g_die[2] = g_coin = g_coinlbl = g_sum = g_hint = NULL;
    s_roll = 0; s_view = 0; s_have_last = false;
}

const app_t app_dice = { "dice", COL_TXT, dice_enter, dice_tick, dice_exit, dice_back };
