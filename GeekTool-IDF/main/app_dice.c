// 摇骰子 / 抛硬币 —— 骰子占满主屏(甩动或点屏触发,翻滚减速定格)。
// 主视图右上小齿轮 → 进本 app 独立设置页(单独一页选 1/2/3 颗骰子 或 硬币,存 NVS);
// 设置页右滑/‹ 返回主视图。骰子全同(≥2 颗)→ 豹子变红;硬币单独走上抛/翻面/落地状态。
#include "app.h"
#include "imu.h"
#include "ui_update.h"
#include "lvgl_compat.h"
#include "settings.h"
#include "esp_random.h"
#include <stdio.h>
#include <math.h>

#define DCY        228           // 骰体/硬币竖直中心(主角,基本居中偏下)
#define ROLL_TICKS 20
#define M_COIN     3             // 模式 3 = 硬币
#define COIN_D     184
#define COIN_Y     248
#define COIN_LIFT  58            // 顶点币缘 y=98,避开圆屏顶部导航
#define COIN_FLY_MS 1040
#define COIN_SETTLE_MS 180
#define COIN_PI    3.14159265f
LV_FONT_DECLARE(font_coin_52);

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
static bool  s_coin_toss, s_imu_ready;
static int   s_coin_from, s_coin_target, s_coin_side;
static uint32_t s_coin_elapsed, s_coin_at, s_sensor_at;

static lv_obj_t *g_pip[3][6];
static lv_obj_t *g_main, *g_set;         // 主视图容器 / 设置页容器
static lv_obj_t *g_stage, *g_die[3], *g_pips[3], *g_coin, *g_coinlbl, *g_sum, *g_hint;
static lv_obj_t *g_coin_name, *g_shadow;

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
    ui_obj_set_event_bubble(d, true);
    return d;
}

static void draw_face(int i, int v, uint32_t col) {
    int c = s_diesz / 2;
    for (int k = 0; k < 6; k++) {
        lv_obj_t *dot = g_pip[i][k];
        if (k >= FACE[v].cnt) { ui_obj_set_hidden(dot, true); continue; }
        lv_obj_set_pos(dot, c + FACE[v].cx[k] * s_pipo - s_pipr,
                            c + FACE[v].cy[k] * s_pipo - s_pipr);
        ui_bg_color(dot, col); ui_obj_set_hidden(dot, false);
    }
}

static void coin_ring(lv_layer_t *layer, int x, int y, int r, int width, uint32_t color) {
    lv_draw_arc_dsc_t d; lv_draw_arc_dsc_init(&d);
    d.center = (lv_point_t){x, y}; d.radius = r; d.width = width;
    d.color = lv_color_hex(color); d.start_angle = 0; d.end_angle = 360;
    lv_draw_arc(layer, &d);
}

static void coin_draw(lv_event_t *e) {
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a; lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    int x = a.x1 + COIN_D / 2, y = a.y1 + COIN_D / 2;
    coin_ring(layer, x, y, 86, 5, 0x68686c);
    coin_ring(layer, x, y, 78, 1, 0x929296);
    // Engraved rim is one draw callback, rather than dozens of moving child objects.
    for (int i = 0; i < 48; ++i) {
        float angle = i * (2 * COIN_PI / 48);
        lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d);
        d.p1 = (lv_point_precise_t){x + cosf(angle) * 82, y + sinf(angle) * 82};
        d.p2 = (lv_point_precise_t){x + cosf(angle) * 85, y + sinf(angle) * 85};
        d.width = 1; d.color = lv_color_hex(0xc8c8cc);
        lv_draw_line(layer, &d);
    }
}

static void coin_face(int side) {
    s_coin_side = side;
    const char *mark = settings_lang() ? (side ? "正" : "反") : (side ? "H" : "T");
    ui_text(g_coinlbl, mark);
    ui_text(g_coin_name, side ? "HEADS" : "TAILS");
    lv_obj_set_style_text_color(g_coinlbl, lv_color_hex(side ? COL_TXT : COL_RED), 0);
}

static const char *coin_hint(void) {
    if (settings_lang()) return tr(s_imu_ready ? S_DICE_HINT : S_DICE_TAP);
    // The footer is narrower than the center of the round screen.
    return s_imu_ready ? "SHAKE / TAP" : "TAP TO TOSS";
}

static void coin_pose(int lift, int scale_y, int rotation) {
    lv_obj_set_y(g_coin, COIN_Y - COIN_D / 2 - lift);
    lv_obj_set_style_transform_scale_y(g_coin, scale_y, 0);
    lv_obj_set_style_transform_rotation(g_coin, rotation, 0);
    int width = 86 - lift / 2;
    lv_obj_set_width(g_shadow, width);
    lv_obj_set_x(g_shadow, 233 - width / 2);
    ui_bg_opa(g_shadow, (lv_opa_t)(100 - lift));
}

/* 主视图舞台:按模式建骰子群或硬币(切模式/进入时调) */
static void build_stage(void) {
    lv_obj_clean(g_stage);
    g_die[0] = g_die[1] = g_die[2] = g_coin = g_coinlbl = NULL;
    g_coin_name = g_shadow = NULL;
    s_coin_toss = false;

    if (s_mode == M_COIN) {
        g_shadow = lv_obj_create(g_stage);
        lv_obj_remove_style_all(g_shadow);
        lv_obj_set_size(g_shadow, 86, 5);
        lv_obj_set_pos(g_shadow, 190, 352);
        lv_obj_set_style_radius(g_shadow, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(g_shadow, lv_color_hex(0x929296), 0);
        lv_obj_set_style_bg_opa(g_shadow, 100, 0);
        ui_obj_set_scrollable(g_shadow, false); ui_obj_set_clickable(g_shadow, false);
        g_coin = lv_obj_create(g_stage);
        lv_obj_remove_style_all(g_coin);
        lv_obj_set_size(g_coin, COIN_D, COIN_D);
        lv_obj_set_pos(g_coin, 233 - COIN_D / 2, COIN_Y - COIN_D / 2);
        lv_obj_set_style_radius(g_coin, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(g_coin, lv_color_hex(0x141416), 0);
        lv_obj_set_style_bg_opa(g_coin, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(g_coin, 4, 0);
        lv_obj_set_style_border_color(g_coin, lv_color_hex(0xe4e4e6), 0);
        lv_obj_set_style_transform_pivot_x(g_coin, COIN_D / 2, 0);
        lv_obj_set_style_transform_pivot_y(g_coin, COIN_D / 2, 0);
        ui_obj_set_scrollable(g_coin, false); ui_obj_set_clickable(g_coin, false);
        ui_obj_set_event_bubble(g_coin, true);
        lv_obj_add_event_cb(g_coin, coin_draw, LV_EVENT_DRAW_MAIN, NULL);
        g_coinlbl = lv_label_create(g_coin);
        lv_obj_set_style_text_font(g_coinlbl, &font_coin_52, 0);
        lv_obj_align(g_coinlbl, LV_ALIGN_CENTER, 0, -14);
        ui_obj_set_event_bubble(g_coinlbl, true);
        g_coin_name = lv_label_create(g_coin);
        lv_obj_set_style_text_font(g_coin_name, UI_FONT_M, 0);
        lv_obj_set_style_text_color(g_coin_name, lv_color_hex(0x929296), 0);
        lv_obj_set_style_text_letter_space(g_coin_name, 2, 0);
        lv_obj_align(g_coin_name, LV_ALIGN_CENTER, 0, 35);
        coin_face(s_val[0]);
        lv_obj_set_style_text_font(g_sum, UI_FONT_L, 0);
        lv_obj_set_style_text_color(g_sum, lv_color_hex(COL_TXT), 0);
        lv_obj_align(g_sum, LV_ALIGN_TOP_MID, 0, 376);
        lv_obj_align(g_hint, LV_ALIGN_TOP_MID, 0, 410);
        return;
    }

    lv_obj_set_style_text_font(g_sum, UI_FONT_SYM, 0);
    lv_obj_align(g_sum, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_align(g_hint, LV_ALIGN_TOP_MID, 0, 92);
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
        ui_obj_set_event_bubble(g_die[i], true);
        g_pips[i] = lv_obj_create(g_die[i]);
        lv_obj_remove_style_all(g_pips[i]);
        lv_obj_set_size(g_pips[i], s_diesz, s_diesz);
        ui_obj_set_event_bubble(g_pips[i], true);
        for (int k = 0; k < 6; k++) g_pip[i][k] = mkdot(g_pips[i], s_diesz / 2, s_diesz / 2, s_pipr, COL_TXT);
    }
}

static void render(void) {
    if (s_mode == M_COIN) {
        if (!g_coinlbl) return;
        coin_face(s_val[0]);
        ui_text(g_sum, tr(s_val[0] ? S_HEADS : S_TAILS));
        ui_text(g_hint, coin_hint());
        return;
    }
    int N = s_mode + 1;
    bool same = (N >= 2);
    for (int i = 1; i < N; i++) if (s_val[i] != s_val[0]) same = false;
    uint32_t col = same ? COL_RED : COL_TXT;
    int sum = 0;
    for (int i = 0; i < N; i++) { draw_face(i, s_val[i], col); sum += s_val[i]; }
    char b[16]; snprintf(b, sizeof b, "%d", sum);
    ui_text(g_sum, b);
    lv_obj_set_style_text_color(g_sum, lv_color_hex(same ? COL_RED : COL_TXT2), 0);
    ui_text(g_hint, same ? tr(S_DICE_DOUBLE) : "");
}

static void roll_once(void) {
    for (int i = 0; i < s_mode + 1; i++) s_val[i] = 1 + (int)(esp_random() % 6);
    render();
}

static void finish_coin(void) {
    s_coin_toss = false;
    s_val[0] = s_coin_target;
    coin_pose(0, 256, 0);
    render();
}

static void coin_tick(void) {
    if (!s_coin_toss) return;
    uint32_t now = lv_tick_get();
    s_coin_elapsed += now - s_coin_at; s_coin_at = now;
    if (s_coin_elapsed >= COIN_FLY_MS + COIN_SETTLE_MS) { finish_coin(); return; }
    if (s_coin_elapsed < COIN_FLY_MS) {
        float t = (float)s_coin_elapsed / COIN_FLY_MS;
        // Integer half-turns finish on the single sampled outcome; visual flips consume no entropy.
        int turns = 6 + (s_coin_from != s_coin_target);
        float angle = turns * COIN_PI * (1 - (1 - t) * (1 - t));
        float face = cosf(angle);
        int side = face < 0 ? !s_coin_from : s_coin_from;
        if (side != s_coin_side) coin_face(side);
        int scale = (int)lroundf(fmaxf(.055f, fabsf(face)) * 256);
        coin_pose((int)lroundf(COIN_LIFT * 4 * t * (1 - t)), scale,
                  (int)lroundf(sinf(t * COIN_PI) * 60));
    } else {
        if (s_coin_side != s_coin_target) coin_face(s_coin_target);
        float t = (float)(s_coin_elapsed - COIN_FLY_MS) / COIN_SETTLE_MS;
        float bounce = sinf(t * COIN_PI);
        coin_pose((int)lroundf(bounce * 6), 256 - (int)lroundf(bounce * 12), 0);
    }
}

static void start_roll(void) {
    if (s_roll > 0 || s_coin_toss || s_view != 0 || !g_stage) return;
    if (s_mode == M_COIN) {
        s_coin_from = s_val[0];
        s_coin_target = (int)(esp_random() & 1);
        s_coin_elapsed = 0; s_coin_at = lv_tick_get(); s_coin_toss = true;
        ui_text(g_sum, "...");
        return;
    }
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
    if (s_coin_toss) finish_coin();
    s_roll = 0;
    s_have_last = false;
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
    ui_obj_set_scrollable(list, false);
    ui_obj_set_event_bubble(list, true);
    for (int i = 0; i < 4; i++) {
        lv_obj_t *it = lv_obj_create(list);
        lv_obj_remove_style_all(it);
        lv_obj_set_size(it, 240, 54);
        lv_obj_set_style_radius(it, 14, 0);
        lv_obj_set_style_bg_color(it, lv_color_hex(i == s_mode ? 0x2a1216 : 0x16161c), 0);
        lv_obj_set_style_bg_opa(it, LV_OPA_COVER, 0);
        ui_obj_set_clickable(it, true);
        ui_obj_set_event_bubble(it, true);
        lv_obj_add_event_cb(it, setrow_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *l = lv_label_create(it);
        lv_obj_set_style_text_font(l, UI_FONT_L, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(i == s_mode ? COL_RED : COL_TXT), 0);
        lv_label_set_text(l, mode_label(i));
        lv_obj_center(l);
    }

    s_view = 1;
    ui_obj_set_hidden(g_main, true);
    ui_obj_set_hidden(g_set, false);
    launcher_set_title(tr(S_DICE_MODE));
}

// 框架返回:设置页 → 回主视图(消费);主视图 → 返 false 退出 app
static bool dice_back(void) {
    if (s_view == 0) return false;
    s_view = 0;
    ui_obj_set_hidden(g_set, true);
    s_val[0] = s_val[1] = s_val[2] = (s_mode == M_COIN) ? 0 : 1;
    build_stage();                       // 模式可能变了,重建主视图舞台
    render();
    ui_obj_set_hidden(g_main, false);
    s_sensor_at = lv_tick_get(); s_have_last = false;
    launcher_set_title(s_mode == M_COIN ? tr(S_COIN) : tr_app_name("dice"));
    return true;
}

static void dice_enter(lv_obj_t *parent) {
    s_mode = settings_dice();
    if (s_mode == M_COIN) s_val[0] = 0;
    else for (int i = 0; i < 3; ++i) if (s_val[i] < 1 || s_val[i] > 6) s_val[i] = 1;
    s_imu_ready = imu_init();

    /* ---- 主视图 ---- */
    g_main = lv_obj_create(parent);
    lv_obj_remove_style_all(g_main);
    lv_obj_set_size(g_main, lv_pct(100), lv_pct(100));
    ui_obj_set_scrollable(g_main, false);
    ui_obj_set_event_bubble(g_main, true);

    g_stage = lv_obj_create(g_main);
    lv_obj_remove_style_all(g_stage);
    lv_obj_set_size(g_stage, lv_pct(100), lv_pct(100));
    ui_obj_set_scrollable(g_stage, false);
    ui_obj_set_event_bubble(g_stage, true);

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
    ui_obj_set_scrollable(hit, false);
    ui_obj_set_clickable(hit, true);
    ui_obj_set_event_bubble(hit, true);
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
    ui_obj_set_scrollable(gear, false);
    ui_obj_set_clickable(gear, true);
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
    ui_obj_set_scrollable(g_set, false);
    ui_obj_set_event_bubble(g_set, true);
    ui_obj_set_hidden(g_set, true);

    s_view = 0;
    build_stage();
    lv_label_set_text(g_hint, tr(s_imu_ready ? S_DICE_HINT : S_DICE_TAP));
    s_roll = 0; s_have_last = false;
    s_sensor_at = lv_tick_get();
    render();
    launcher_set_title(s_mode == M_COIN ? tr(S_COIN) : tr_app_name("dice"));
}

static void dice_tick(void) {
    if (!g_stage || s_view != 0) return;
    if (s_mode == M_COIN) coin_tick();
    uint32_t now = lv_tick_get();
    if (now - s_sensor_at < 50) return;
    s_sensor_at += ((now - s_sensor_at) / 50) * 50;

    float ax, ay, az;
    if (imu_read_accel(&ax, &ay, &az) && isfinite(ax) && isfinite(ay) && isfinite(az)) {
        if (s_have_last) {
            float jerk = fabsf(ax - s_ax) + fabsf(ay - s_ay) + fabsf(az - s_az);
            if (jerk > 1.1f) start_roll();
        }
        s_ax = ax; s_ay = ay; s_az = az; s_have_last = true;
    } else s_have_last = false;

    if (s_roll > 0) {
        if (--s_next <= 0) { roll_once(); int done = ROLL_TICKS - s_roll; s_next = 1 + done / 4; }
        s_roll--;
        if (s_roll == 0) roll_once();
    }
}

static void dice_visibility(bool visible) {
    (void)visible;
    // Launcher suspends tick while covered: resume the toss without jumping or a stale shake sample.
    s_coin_at = s_sensor_at = lv_tick_get();
    s_have_last = false;
}

static void dice_exit(void) {
    g_main = g_set = g_stage = g_die[0] = g_die[1] = g_die[2] = g_coin = g_coinlbl = g_sum = g_hint = NULL;
    g_coin_name = g_shadow = NULL; s_coin_toss = false;
    s_roll = 0; s_view = 0; s_have_last = false;
}

const app_t app_dice = { "dice", COL_TXT, dice_enter, dice_tick, dice_exit, dice_back, 20, dice_visibility };
