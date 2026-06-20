// 启动器(小面积运动设计)+ App 框架 + 导航 —— LVGL 9
// 静止黑底,居中一个大图标;切换只动中心一小块(短滑+淡入淡出,不整屏滑)→ 从设计上避开撕裂。
#include "app.h"
#include "power.h"
#include "lock.h"

#define ICON 196
#define SWAP_MS    220    // 切换动画总时长(ms);半程滑出、半程滑入
#define SWAP_SLIDE 56     // 中心块滑动幅度(px,越小越不易撕裂)

// 注册表
const app_t *const APPS[] = { &app_wifi, &app_i2c, &app_sys, &app_ota };
const int APP_COUNT = sizeof(APPS) / sizeof(APPS[0]);

static lv_obj_t *launcher_screen, *app_screen;
static lv_obj_t *g_icon, *g_letter, *g_name, *g_title, *g_back, *g_batt, *g_bolt;
static const app_t *cur_app;
static int cur, pending_app;
static pwr_state_t s_last_pwr = PWR_UNKNOWN;

/* ---- 瞬时替换居中 app 的内容(只改中心一小块) ---- */
static void apply_app(int i) {
    cur = i;
    lv_obj_set_style_bg_color(g_icon, lv_color_hex(APPS[i]->color), 0);
    char letter[2] = { APPS[i]->name[0], 0 };
    lv_label_set_text(g_letter, letter);
    lv_label_set_text(g_name, APPS[i]->name);
}

/* ---- 小面积切换动画:中心图标+名字 半程滑出淡出 → 中点换内容 → 反向滑入淡入。
       黑底全程不动,只重绘中心一小块,从设计上避开整屏滑的撕裂。 ---- */
static int  swap_exit_x;     // 本次滑出的半程目标 x(正负取决于左右滑)
static bool swapped;         // 本次动画是否已在中点换过内容

static void swap_exec(void *var, int32_t v) {        // v: 0..256
    int32_t  x;
    lv_opa_t opa;
    if (v < 128) {                                   // 前半:旧内容滑出 + 淡出
        x   = swap_exit_x * v / 128;
        opa = LV_OPA_COVER - LV_OPA_COVER * v / 128;
    } else {                                         // 后半:新内容从反向滑入 + 淡入
        if (!swapped) { apply_app(pending_app); swapped = true; }
        int32_t w = v - 128;                         // 0..128
        x   = -swap_exit_x * (128 - w) / 128;
        opa = LV_OPA_COVER * w / 128;
    }
    lv_obj_set_style_translate_x(g_icon, x, 0);
    lv_obj_set_style_opa(g_icon, opa, 0);
    lv_obj_set_style_translate_x(g_name, x, 0);
    lv_obj_set_style_opa(g_name, opa, 0);
}

static void nav(int dir) {
    if (lv_anim_get(&swap_exit_x, swap_exec)) return;  // 动画进行中,忽略连击
    pending_app = (cur + dir + APP_COUNT) % APP_COUNT;
    swap_exit_x = -SWAP_SLIDE * dir;                   // 左滑(下一个)向左出,右滑反之
    swapped     = false;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &swap_exit_x);                 // 占位 var,仅用于识别动画句柄
    lv_anim_set_exec_cb(&a, swap_exec);
    lv_anim_set_values(&a, 0, 256);
    lv_anim_set_duration(&a, SWAP_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

static void launcher_gesture_cb(lv_event_t *e) {
    lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_active());
    if (d == LV_DIR_LEFT)       nav(+1);
    else if (d == LV_DIR_RIGHT) nav(-1);
}
static void arrow_prev_cb(lv_event_t *e) { nav(-1); }
static void arrow_next_cb(lv_event_t *e) { nav(+1); }

/* ---- App 生命周期 ---- */
static void app_tick_timer(lv_timer_t *t) {
    if (cur_app && cur_app->tick) cur_app->tick();
}
static void app_gesture_cb(lv_event_t *e) {
    if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_RIGHT) go_home();
}
static void back_cb(lv_event_t *e) { go_home(); }

static void icon_cb(lv_event_t *e) {
    cur_app = APPS[cur];
    app_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(app_screen, lv_color_black(), 0);
    lv_obj_remove_flag(app_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(app_screen, app_gesture_cb, LV_EVENT_GESTURE, NULL);
    if (cur_app->enter) cur_app->enter(app_screen);
    lv_label_set_text(g_title, cur_app->name);
    lv_obj_remove_flag(g_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g_back, LV_OBJ_FLAG_HIDDEN);
    lv_screen_load(app_screen);
}

void go_home(void) {
    if (!cur_app) return;
    lv_obj_add_flag(g_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_back, LV_OBJ_FLAG_HIDDEN);
    if (cur_app->exit) cur_app->exit();
    cur_app = NULL;
    lv_screen_load(launcher_screen);
    if (app_screen) { lv_obj_delete_async(app_screen); app_screen = NULL; }
}

/* ---- 顶层悬浮:电量环 + 标题 + 返回 ---- */
static void build_overlay(void) {
    lv_obj_t *top = lv_layer_top();

    g_batt = lv_arc_create(top);
    lv_obj_set_size(g_batt, 458, 458);
    lv_obj_center(g_batt);
    lv_arc_set_rotation(g_batt, 270);
    lv_arc_set_bg_angles(g_batt, 0, 360);
    lv_arc_set_range(g_batt, 0, 100);
    lv_arc_set_value(g_batt, 0);                 // 真实电量由 battery_timer_cb 填
    lv_obj_remove_style(g_batt, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(g_batt, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(g_batt, lv_color_hex(0x15151a), LV_PART_MAIN);
    lv_obj_set_style_arc_width(g_batt, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(g_batt, lv_color_hex(COL_RING), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(g_batt, 8, LV_PART_INDICATOR);

    // 充电状态图标(⚡):顶端居中,默认隐藏。充电时呼吸、充满时常亮(只动这一小块)
    g_bolt = lv_label_create(top);
    lv_label_set_text(g_bolt, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_font(g_bolt, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_bolt, lv_color_hex(COL_RING), 0);
    lv_obj_align(g_bolt, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_add_flag(g_bolt, LV_OBJ_FLAG_HIDDEN);

    g_title = lv_label_create(top);
    lv_label_set_text(g_title, "");
    lv_obj_set_style_text_color(g_title, lv_color_hex(COL_TXT), 0);
    lv_obj_align(g_title, LV_ALIGN_TOP_MID, 0, 46);
    lv_obj_add_flag(g_title, LV_OBJ_FLAG_HIDDEN);

    g_back = lv_obj_create(top);
    lv_obj_set_size(g_back, 46, 34);
    lv_obj_set_style_radius(g_back, 17, 0);
    lv_obj_set_style_border_width(g_back, 0, 0);
    lv_obj_set_style_bg_color(g_back, lv_color_hex(0x26262c), 0);
    lv_obj_align(g_back, LV_ALIGN_TOP_MID, -96, 42);
    lv_obj_remove_flag(g_back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(g_back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(bl, lv_color_white(), 0);
    lv_obj_center(bl);
    lv_obj_add_flag(g_back, LV_OBJ_FLAG_HIDDEN);
}

/* ---- 电量/充电状态:周期读 AXP2101,更新电量环颜色 + ⚡ 图标 ---- */
static void bolt_opa_cb(void *obj, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0); }

static void bolt_breath(bool on) {
    lv_anim_delete(g_bolt, bolt_opa_cb);
    if (!on) { lv_obj_set_style_opa(g_bolt, LV_OPA_COVER, 0); return; }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, g_bolt);
    lv_anim_set_exec_cb(&a, bolt_opa_cb);
    lv_anim_set_values(&a, LV_OPA_50, LV_OPA_COVER);
    lv_anim_set_duration(&a, 700);
    lv_anim_set_playback_duration(&a, 700);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

static void battery_timer_cb(lv_timer_t *t) {
    int soc; pwr_state_t st;
    if (!power_read(&soc, &st)) return;          // 读失败:保持上次显示
    if (soc < 0)   soc = 0;
    if (soc > 100) soc = 100;
    lv_arc_set_value(g_batt, soc);

    uint32_t col;
    switch (st) {
        case PWR_CHARGING: col = COL_WIFI; break;                 // 青蓝 = 充电
        case PWR_FULL:     col = COL_OK;   break;                 // 绿 = 充满
        default:           col = (soc > 50) ? COL_OK              // 放电:绿/琥珀/红
                                 : (soc > 20 ? COL_I2C : COL_WARN);
    }
    lv_obj_set_style_arc_color(g_batt, lv_color_hex(col), LV_PART_INDICATOR);

    bool plugged = (st == PWR_CHARGING || st == PWR_FULL);
    lv_obj_set_style_text_color(g_bolt, lv_color_hex(col), 0);
    if (plugged) lv_obj_remove_flag(g_bolt, LV_OBJ_FLAG_HIDDEN);
    else         lv_obj_add_flag(g_bolt, LV_OBJ_FLAG_HIDDEN);

    if (st != s_last_pwr) {
        bolt_breath(st == PWR_CHARGING);         // 仅充电时呼吸,充满则常亮
        s_last_pwr = st;
    }
}

void launcher_start(void) {
    build_overlay();

    launcher_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(launcher_screen, lv_color_black(), 0);
    lv_obj_remove_flag(launcher_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(launcher_screen, launcher_gesture_cb, LV_EVENT_GESTURE, NULL);

    g_icon = lv_obj_create(launcher_screen);
    lv_obj_set_size(g_icon, ICON, ICON);
    lv_obj_set_style_radius(g_icon, ICON / 2, 0);
    lv_obj_set_style_border_width(g_icon, 0, 0);
    lv_obj_align(g_icon, LV_ALIGN_CENTER, 0, -16);
    lv_obj_remove_flag(g_icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_icon, LV_OBJ_FLAG_EVENT_BUBBLE);   // 在图标上滑动也能切换
    lv_obj_add_event_cb(g_icon, icon_cb, LV_EVENT_CLICKED, NULL);

    g_letter = lv_label_create(g_icon);
    lv_obj_set_style_text_color(g_letter, lv_color_black(), 0);
    lv_obj_set_style_text_font(g_letter, &lv_font_montserrat_20, 0);
    lv_obj_center(g_letter);

    g_name = lv_label_create(launcher_screen);
    lv_obj_set_style_text_color(g_name, lv_color_hex(COL_TXT), 0);
    lv_obj_set_style_text_font(g_name, &lv_font_montserrat_20, 0);
    lv_obj_align(g_name, LV_ALIGN_CENTER, 0, ICON / 2 + 14);

    lv_obj_t *al = lv_label_create(launcher_screen);
    lv_label_set_text(al, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(al, lv_color_hex(COL_TXT2), 0);
    lv_obj_align(al, LV_ALIGN_LEFT_MID, 14, 0);
    lv_obj_add_flag(al, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(al, 24);
    lv_obj_add_event_cb(al, arrow_prev_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ar = lv_label_create(launcher_screen);
    lv_label_set_text(ar, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(ar, lv_color_hex(COL_TXT2), 0);
    lv_obj_align(ar, LV_ALIGN_RIGHT_MID, -14, 0);
    lv_obj_add_flag(ar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(ar, 24);
    lv_obj_add_event_cb(ar, arrow_next_cb, LV_EVENT_CLICKED, NULL);

    apply_app(0);
    lv_screen_load(launcher_screen);

    lv_timer_create(app_tick_timer, 50, NULL);   // 周期跑当前 app 的 tick

    power_init();
    battery_timer_cb(NULL);                       // 开机立即读一次电量
    lv_timer_create(battery_timer_cb, 2000, NULL);

    lock_init();                                  // 锁屏 / 表盘 / 实体键 / 省电
}
