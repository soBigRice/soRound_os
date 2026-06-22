// 锁屏下拉快捷面板(Nothing 单色:黑底 / 灰描边 / 红强调)。
// 从表盘顶部下拉打开:WiFi、always-on(AOD)、静音三个圆形开关 + 亮度滑条;上滑/点把手关闭。
// 整块面板用 translate_y 从屏幕上方滑入/滑出 → 经典通知栏下拉动画。
#include "quickpanel.h"
#include "app.h"
#include "settings.h"
#include "board_config.h"

#define QP_H        LCD_V_RES      // 面板高度 = 屏高(466),收起时整体上移一屏
#define QP_ANIM_IN  300            // 下拉滑入时长(ms)
#define QP_ANIM_OUT 260            // 收起滑出时长(ms)
#define CHIP_SZ     84             // 圆形开关直径
#define CHIP_DX     118            // 左右两侧 chip 的水平偏移
#define CHIP_Y      150            // chip 顶部 y(挂在最宽的竖直中段,圆屏不会被裁)

typedef struct { lv_obj_t *btn, *icon; } chip_t;

static lv_obj_t *qp_panel, *qp_bright;
static chip_t    qp_wifi, qp_aod, qp_silent;
static bool      qp_open;

/* ---- chip 视觉:开=红描边 + 红图标 + 暗红底;关=灰描边 + 灰图标 ---- */
static void chip_set(chip_t *c, bool on) {
    lv_obj_set_style_border_color(c->btn, lv_color_hex(on ? COL_RED : COL_TXT2), 0);
    lv_obj_set_style_bg_color(c->btn, lv_color_hex(on ? 0x2a0c10 : 0x141418), 0);
    lv_obj_set_style_text_color(c->icon, lv_color_hex(on ? COL_RED : COL_TXT2), 0);
}

static chip_t mk_chip(lv_obj_t *par, const char *sym, const char *label, int dx, lv_event_cb_t cb) {
    chip_t c;
    c.btn = lv_obj_create(par);
    lv_obj_set_size(c.btn, CHIP_SZ, CHIP_SZ);
    lv_obj_set_style_radius(c.btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(c.btn, 2, 0);
    lv_obj_set_style_bg_opa(c.btn, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(c.btn, 0, 0);
    lv_obj_align(c.btn, LV_ALIGN_TOP_MID, dx, CHIP_Y);
    lv_obj_remove_flag(c.btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(c.btn, LV_OBJ_FLAG_GESTURE_BUBBLE);   // chip 上的手势不冒泡(免误触开/关面板)
    lv_obj_add_event_cb(c.btn, cb, LV_EVENT_CLICKED, NULL);

    c.icon = lv_label_create(c.btn);
    lv_obj_set_style_text_font(c.icon, UI_FONT_SYM, 0);
    lv_label_set_text(c.icon, sym);
    lv_obj_center(c.icon);

    lv_obj_t *t = lv_label_create(par);
    lv_obj_set_style_text_font(t, UI_FONT_M, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(COL_TXT2), 0);
    lv_label_set_text(t, label);
    lv_obj_align(t, LV_ALIGN_TOP_MID, dx, CHIP_Y + CHIP_SZ + 8);
    return c;
}

/* ---- 开关回调:即时生效 + 触发活动(避免面板开着时屏幕变暗) ---- */
static void wifi_cb(lv_event_t *e) {
    (void)e;
    bool on = !wifi_service_enabled();
    wifi_service_set_enabled(on);
    chip_set(&qp_wifi, on);
    lv_display_trigger_activity(NULL);
}
static void aod_cb(lv_event_t *e) {
    (void)e;
    bool on = (settings_idle_mode() != IDLE_AOD);
    settings_set_idle_mode(on ? IDLE_AOD : IDLE_OFF);
    settings_save();
    lv_label_set_text(qp_aod.icon, on ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_EYE_CLOSE);
    chip_set(&qp_aod, on);
    lv_display_trigger_activity(NULL);
}
static void silent_cb(lv_event_t *e) {
    (void)e;
    bool on = !settings_silent();
    settings_set_silent(on ? 1 : 0);
    settings_save();
    lv_label_set_text(qp_silent.icon, on ? LV_SYMBOL_MUTE : LV_SYMBOL_BELL);
    chip_set(&qp_silent, on);
    lv_display_trigger_activity(NULL);
}
static void bright_cb(lv_event_t *e) {
    settings_set_brightness((uint8_t)lv_slider_get_value(lv_event_get_target_obj(e)));
    lv_display_trigger_activity(NULL);
}
static void bright_released(lv_event_t *e) { (void)e; settings_save(); }   // 松手写 NVS

/* ---- 打开时把开关/滑条同步到当前真实状态 ---- */
static void qp_sync(void) {
    bool w = wifi_service_enabled();
    chip_set(&qp_wifi, w);
    bool a = (settings_idle_mode() == IDLE_AOD);
    lv_label_set_text(qp_aod.icon, a ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_EYE_CLOSE);
    chip_set(&qp_aod, a);
    bool s = settings_silent();
    lv_label_set_text(qp_silent.icon, s ? LV_SYMBOL_MUTE : LV_SYMBOL_BELL);
    chip_set(&qp_silent, s);
    lv_slider_set_value(qp_bright, settings_brightness(), LV_ANIM_OFF);
}

/* ---- 下拉/收起动画(整块面板 translate_y) ---- */
static void qp_y_cb(void *var, int32_t v) { lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0); }
static void qp_closed_cb(lv_anim_t *a) { (void)a; lv_obj_add_flag(qp_panel, LV_OBJ_FLAG_HIDDEN); }

static void qp_gesture_cb(lv_event_t *e) {
    (void)e;
    if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_TOP) quickpanel_close();
}
static void handle_cb(lv_event_t *e) { (void)e; quickpanel_close(); }

void quickpanel_open(void) {
    if (qp_open || !qp_panel) return;
    qp_open = true;
    qp_sync();
    lv_anim_delete(qp_panel, qp_y_cb);
    lv_obj_remove_flag(qp_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(qp_panel);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, qp_panel);
    lv_anim_set_exec_cb(&a, qp_y_cb);
    lv_anim_set_values(&a, lv_obj_get_style_translate_y(qp_panel, 0), 0);   // 从当前位置滑到归位
    lv_anim_set_duration(&a, QP_ANIM_IN);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
    lv_display_trigger_activity(NULL);
}

void quickpanel_close(void) {
    if (!qp_open || !qp_panel) return;
    qp_open = false;
    lv_anim_delete(qp_panel, qp_y_cb);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, qp_panel);
    lv_anim_set_exec_cb(&a, qp_y_cb);
    lv_anim_set_values(&a, lv_obj_get_style_translate_y(qp_panel, 0), -QP_H);
    lv_anim_set_duration(&a, QP_ANIM_OUT);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&a, qp_closed_cb);
    lv_anim_start(&a);
}

bool quickpanel_is_open(void) { return qp_open; }

void quickpanel_hide(void) {
    if (!qp_panel) return;
    lv_anim_delete(qp_panel, qp_y_cb);
    qp_open = false;
    lv_obj_set_style_translate_y(qp_panel, -QP_H, 0);
    lv_obj_add_flag(qp_panel, LV_OBJ_FLAG_HIDDEN);
}

void quickpanel_init(lv_obj_t *parent) {
    qp_panel = lv_obj_create(parent);
    lv_obj_remove_style_all(qp_panel);
    lv_obj_set_size(qp_panel, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(qp_panel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(qp_panel, LV_OPA_COVER, 0);
    lv_obj_remove_flag(qp_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(qp_panel, LV_OBJ_FLAG_GESTURE_BUBBLE);   // 面板手势自己处理,不冒泡到表盘(免换表盘)
    lv_obj_add_event_cb(qp_panel, qp_gesture_cb, LV_EVENT_GESTURE, NULL);

    // 顶部把手(可点收起;上滑同样收起)
    lv_obj_t *handle = lv_obj_create(qp_panel);
    lv_obj_remove_style_all(handle);
    lv_obj_set_size(handle, 46, 6);
    lv_obj_set_style_radius(handle, 3, 0);
    lv_obj_set_style_bg_color(handle, lv_color_hex(COL_TXT2), 0);
    lv_obj_set_style_bg_opa(handle, LV_OPA_COVER, 0);
    lv_obj_align(handle, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_add_flag(handle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(handle, 30);
    lv_obj_add_event_cb(handle, handle_cb, LV_EVENT_CLICKED, NULL);

    // 三个圆形快捷开关(图标用带符号 montserrat,文字用点阵 unscii)
    qp_wifi   = mk_chip(qp_panel, LV_SYMBOL_WIFI,     "wifi",      -CHIP_DX, wifi_cb);
    qp_aod    = mk_chip(qp_panel, LV_SYMBOL_EYE_OPEN, "always-on", 0,        aod_cb);
    qp_silent = mk_chip(qp_panel, LV_SYMBOL_BELL,     "silent",    CHIP_DX,  silent_cb);

    // 亮度滑条
    lv_obj_t *bl = lv_label_create(qp_panel);
    lv_obj_set_style_text_font(bl, UI_FONT_M, 0);
    lv_obj_set_style_text_color(bl, lv_color_hex(COL_TXT2), 0);
    lv_label_set_text(bl, "brightness");
    lv_obj_align(bl, LV_ALIGN_TOP_MID, 0, 300);

    qp_bright = lv_slider_create(qp_panel);
    lv_obj_set_width(qp_bright, 260);
    lv_slider_set_range(qp_bright, SETTINGS_BRIGHT_MIN, 0xFF);
    lv_obj_align(qp_bright, LV_ALIGN_TOP_MID, 0, 326);
    lv_obj_remove_flag(qp_bright, LV_OBJ_FLAG_GESTURE_BUBBLE);   // 横向拖动不冒泡成整屏手势
    lv_obj_set_style_bg_color(qp_bright, lv_color_hex(0x33343a), LV_PART_MAIN);
    lv_obj_set_style_bg_color(qp_bright, lv_color_hex(COL_TXT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(qp_bright, lv_color_hex(COL_RED), LV_PART_KNOB);
    lv_obj_add_event_cb(qp_bright, bright_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(qp_bright, bright_released, LV_EVENT_RELEASED, NULL);

    // 底部提示:上滑收起
    lv_obj_t *hint = lv_label_create(qp_panel);
    lv_obj_set_style_text_font(hint, UI_FONT_M, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(COL_TXT2), 0);
    lv_label_set_text(hint, LV_SYMBOL_UP "  close");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 392);

    qp_sync();
    quickpanel_hide();   // 初始:移到屏幕上方 + 隐藏,等下拉
}
