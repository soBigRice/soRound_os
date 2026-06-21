// 设置 app —— 亮度 + 音量 两个滑块。拖动时实时应用,松手写 NVS。Nothing 单色主题(滑块用主题红强调)。
#include "app.h"
#include "settings.h"
#include "audio_out.h"
#include <stdio.h>

static lv_obj_t *g_br_val, *g_vol_val;

static void br_changed(lv_event_t *e) {
    int v = lv_slider_get_value(lv_event_get_target_obj(e));
    settings_set_brightness((uint8_t)v);
    char b[8]; snprintf(b, sizeof b, "%d%%", v * 100 / 255);
    lv_label_set_text(g_br_val, b);
}
static void vol_changed(lv_event_t *e) {
    int v = lv_slider_get_value(lv_event_get_target_obj(e));
    settings_set_volume((uint8_t)v);
    audio_out_set_volume((uint8_t)v);                         // 实时应用到扬声器
    char b[8]; snprintf(b, sizeof b, "%d%%", v);
    lv_label_set_text(g_vol_val, b);
}
static void on_released(lv_event_t *e) { settings_save(); }   // 松手存 NVS
static void vol_blip(lv_event_t *e)   { audio_out_blip(); }   // 松手放一声,听当前音量

static void aod_changed(lv_event_t *e) {                       // always-on:开=低功耗长显 AOD,关=自动熄屏
    bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    settings_set_idle_mode(on ? IDLE_AOD : IDLE_OFF);
    settings_save();
}
static void silent_changed(lv_event_t *e) {                    // 静音:闹钟/提示音关闭
    bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    settings_set_silent(on ? 1 : 0);
    settings_save();
}

static lv_obj_t *mk_row(lv_obj_t *parent, const char *name, int y, int mn, int mx, int val,
                        lv_event_cb_t chg, lv_obj_t **vlabel) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, UI_FONT_L, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TXT), 0);
    lv_label_set_text(lbl, name);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 78, y);

    *vlabel = lv_label_create(parent);
    lv_obj_set_style_text_font(*vlabel, UI_FONT_M, 0);
    lv_obj_set_style_text_color(*vlabel, lv_color_hex(COL_TXT2), 0);
    lv_obj_align(*vlabel, LV_ALIGN_TOP_RIGHT, -78, y + 3);

    lv_obj_t *sl = lv_slider_create(parent);
    lv_obj_set_width(sl, 300);
    lv_slider_set_range(sl, mn, mx);
    lv_slider_set_value(sl, val, LV_ANIM_OFF);
    lv_obj_align(sl, LV_ALIGN_TOP_MID, 0, y + 34);
    lv_obj_remove_flag(sl, LV_OBJ_FLAG_GESTURE_BUBBLE);   // 横向拖动滑块不再冒泡成整屏手势 → 不误触返回
    lv_obj_add_event_cb(sl, chg, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(sl, on_released, LV_EVENT_RELEASED, NULL);
    return sl;
}

static void settings_enter(lv_obj_t *parent) {
    audio_out_init();                 // 起扬声器(音量预览 + 之后的提示音);退出时释放
    char b[8];
    mk_row(parent, "brightness", 92, SETTINGS_BRIGHT_MIN, 0xFF, settings_brightness(), br_changed, &g_br_val);
    snprintf(b, sizeof b, "%d%%", settings_brightness() * 100 / 255);
    lv_label_set_text(g_br_val, b);

    lv_obj_t *vsl = mk_row(parent, "volume", 214, 0, 100, settings_volume(), vol_changed, &g_vol_val);
    lv_obj_add_event_cb(vsl, vol_blip, LV_EVENT_RELEASED, NULL);   // 松手试听当前音量
    snprintf(b, sizeof b, "%d%%", settings_volume());
    lv_label_set_text(g_vol_val, b);

    // always-on:开=低功耗长显,关=自动熄屏
    lv_obj_t *al = lv_label_create(parent);
    lv_obj_set_style_text_font(al, UI_FONT_L, 0);
    lv_obj_set_style_text_color(al, lv_color_hex(COL_TXT), 0);
    lv_label_set_text(al, "always-on");
    lv_obj_align(al, LV_ALIGN_TOP_LEFT, 86, 336);
    lv_obj_t *sw1 = lv_switch_create(parent);
    lv_obj_align(sw1, LV_ALIGN_TOP_RIGHT, -86, 332);
    lv_obj_remove_flag(sw1, LV_OBJ_FLAG_GESTURE_BUBBLE);
    if (settings_idle_mode() == IDLE_AOD) lv_obj_add_state(sw1, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw1, aod_changed, LV_EVENT_VALUE_CHANGED, NULL);

    // silent:静音(闹钟/提示音关闭)
    lv_obj_t *sl2 = lv_label_create(parent);
    lv_obj_set_style_text_font(sl2, UI_FONT_L, 0);
    lv_obj_set_style_text_color(sl2, lv_color_hex(COL_TXT), 0);
    lv_label_set_text(sl2, "silent");
    lv_obj_align(sl2, LV_ALIGN_TOP_LEFT, 86, 406);
    lv_obj_t *sw2 = lv_switch_create(parent);
    lv_obj_align(sw2, LV_ALIGN_TOP_RIGHT, -86, 402);
    lv_obj_remove_flag(sw2, LV_OBJ_FLAG_GESTURE_BUBBLE);
    if (settings_silent()) lv_obj_add_state(sw2, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw2, silent_changed, LV_EVENT_VALUE_CHANGED, NULL);
}

static void settings_exit(void) { audio_out_deinit(); g_br_val = g_vol_val = NULL; }

const app_t app_settings = { "settings", COL_TXT, settings_enter, NULL, settings_exit };
