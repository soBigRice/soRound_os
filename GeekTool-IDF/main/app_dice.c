// 摇骰子 —— QMI8658 加速度计检测甩动(或点屏)→ 两颗骰子翻滚减速定格。
// 点阵风:圆角骰体(深色描白边)+ 3x3 网格点数;翻滚由 launcher 50ms tick 驱动,越滚越慢(ease-out);
// 定格豹子(两颗同点)→ 点数变红强调。Nothing 单色 + 唯一红。
#include "app.h"
#include "imu.h"
#include "esp_random.h"
#include <stdio.h>
#include <math.h>

#define DIE     128            // 骰体边长
#define GAP     28             // 两骰间距
#define DCY     206            // 骰体竖直中心
#define PIP_R   11             // 点半径
#define PIP_O   34             // 点在 3x3 网格上的偏移(中心 ±PIP_O)
#define ROLL_TICKS 20          // 翻滚总步数(×50ms ≈ 1s)

// 6 个面的点位:每面最多 6 点,(col,row) 取值 -1/0/+1(3x3 网格)。cnt=点数。
static const struct { int cnt; int8_t cx[6], cy[6]; } FACE[7] = {
    { 0, {0},{0} },
    { 1, { 0},        { 0} },                                  // 1:中心
    { 2, {-1, 1},     {-1, 1} },                               // 2:对角
    { 3, {-1, 0, 1},  {-1, 0, 1} },                            // 3:对角 + 中心
    { 4, {-1, 1,-1, 1},{-1,-1, 1, 1} },                        // 4:四角
    { 5, {-1, 1, 0,-1, 1},{-1,-1, 0, 1, 1} },                  // 5:四角 + 中心
    { 6, {-1, 1,-1, 1,-1, 1},{-1,-1, 0, 0, 1, 1} },            // 6:两列各三
};

static lv_obj_t *g_die[2], *g_pips[2], *g_sum, *g_hint;
static int   s_val[2] = { 1, 1 };
static int   s_roll, s_next;      // 剩余翻滚步数 / 距下次翻面步数
static float s_ax, s_ay, s_az;    // 上次加速度(算抖动)
static bool  s_have_last;

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

// 在 g_pips[i] 里按点数重画点(点数容器与骰体等大,点相对骰心定位)
static void draw_face(int i, int v, uint32_t col) {
    lv_obj_clean(g_pips[i]);
    const int c = DIE / 2;
    for (int k = 0; k < FACE[v].cnt; k++)
        mkdot(g_pips[i], c + FACE[v].cx[k] * PIP_O, c + FACE[v].cy[k] * PIP_O, PIP_R, col);
}

static void render(void) {
    bool dbl = (s_val[0] == s_val[1]);           // 豹子:点数变红
    uint32_t col = dbl ? COL_RED : COL_TXT;
    for (int i = 0; i < 2; i++) draw_face(i, s_val[i], col);
    char b[16]; snprintf(b, sizeof b, "%d", s_val[0] + s_val[1]);
    lv_label_set_text(g_sum, b);
    lv_obj_set_style_text_color(g_sum, lv_color_hex(dbl ? COL_RED : COL_TXT2), 0);
    lv_label_set_text(g_hint, dbl ? "double!" : "");
}

static void start_roll(void) {
    if (s_roll > 0) return;                       // 已在滚,忽略
    s_roll = ROLL_TICKS;
    s_next = 0;
    lv_label_set_text(g_hint, "");
}

static void tap_cb(lv_event_t *e) { (void)e; start_roll(); }

static void dice_enter(lv_obj_t *parent) {
    static const int cx[2] = { 233 - DIE / 2 - GAP / 2, 233 + DIE / 2 + GAP / 2 };
    for (int i = 0; i < 2; i++) {
        g_die[i] = lv_obj_create(parent);
        lv_obj_remove_style_all(g_die[i]);
        lv_obj_set_size(g_die[i], DIE, DIE);
        lv_obj_set_pos(g_die[i], cx[i] - DIE / 2, DCY - DIE / 2);
        lv_obj_set_style_radius(g_die[i], 24, 0);
        lv_obj_set_style_bg_color(g_die[i], lv_color_hex(0x141418), 0);
        lv_obj_set_style_bg_opa(g_die[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(g_die[i], 2, 0);
        lv_obj_set_style_border_color(g_die[i], lv_color_hex(COL_TXT), 0);
        lv_obj_remove_flag(g_die[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(g_die[i], LV_OBJ_FLAG_EVENT_BUBBLE);
        g_pips[i] = lv_obj_create(g_die[i]);      // 点数层:铺满骰体,点相对定位
        lv_obj_remove_style_all(g_pips[i]);
        lv_obj_set_size(g_pips[i], DIE, DIE);
        lv_obj_set_pos(g_pips[i], 0, 0);
        lv_obj_remove_flag(g_pips[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(g_pips[i], LV_OBJ_FLAG_EVENT_BUBBLE);
    }

    g_sum = lv_label_create(parent);              // 点数和(大字)
    lv_obj_set_style_text_font(g_sum, UI_FONT_SYM, 0);
    lv_obj_set_style_text_color(g_sum, lv_color_hex(COL_TXT2), 0);
    lv_obj_align(g_sum, LV_ALIGN_TOP_MID, 0, 300);

    g_hint = lv_label_create(parent);
    lv_obj_set_style_text_font(g_hint, UI_FONT_M, 0);
    lv_obj_set_style_text_color(g_hint, lv_color_hex(COL_RED), 0);
    lv_obj_align(g_hint, LV_ALIGN_TOP_MID, 0, 116);

    // 全屏透明点击区:点一下 = 摇(不挡右滑返回,手势照常冒泡)
    lv_obj_t *hit = lv_obj_create(parent);
    lv_obj_remove_style_all(hit);
    lv_obj_set_size(hit, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(hit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(hit, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(hit, tap_cb, LV_EVENT_CLICKED, NULL);

    lv_label_set_text(g_hint, imu_init() ? "shake or tap to roll" : "tap to roll");
    lv_obj_set_style_text_color(g_hint, lv_color_hex(COL_TXT2), 0);
    s_roll = 0; s_have_last = false;
    render();
}

static void dice_tick(void) {
    if (!g_die[0]) return;

    // 甩动检测:相邻两次加速度差的绝对和(去掉恒定重力),超阈值即触发
    float ax, ay, az;
    if (imu_read_accel(&ax, &ay, &az)) {
        if (s_have_last) {
            float jerk = fabsf(ax - s_ax) + fabsf(ay - s_ay) + fabsf(az - s_az);
            if (jerk > 1.1f) start_roll();        // ~1.1g 突变 ≈ 明显甩一下
        }
        s_ax = ax; s_ay = ay; s_az = az; s_have_last = true;
    }

    if (s_roll > 0) {
        if (--s_next <= 0) {                       // 翻面:越接近停,间隔越大(ease-out 减速)
            s_val[0] = 1 + (int)(esp_random() % 6);
            s_val[1] = 1 + (int)(esp_random() % 6);
            render();
            int done = ROLL_TICKS - s_roll;        // 0..ROLL_TICKS
            s_next = 1 + done / 4;                 // 间隔 1→6 步逐渐拉长
        }
        s_roll--;
        if (s_roll == 0) {                         // 定格:最终点数
            s_val[0] = 1 + (int)(esp_random() % 6);
            s_val[1] = 1 + (int)(esp_random() % 6);
            render();
        }
    }
}

static void dice_exit(void) {
    g_die[0] = g_die[1] = g_pips[0] = g_pips[1] = g_sum = g_hint = NULL;
    s_roll = 0; s_have_last = false;
}

const app_t app_dice = { "dice", COL_TXT, dice_enter, dice_tick, dice_exit };
