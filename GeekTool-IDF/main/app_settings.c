// 设置 app —— 亮度 + 音量 两个滑块。拖动时实时应用,松手写 NVS。Nothing 单色主题(滑块用主题红强调)。
#include "app.h"
#include "settings.h"
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
    char b[8]; snprintf(b, sizeof b, "%d%%", v);
    lv_label_set_text(g_vol_val, b);
}
static void on_released(lv_event_t *e) { settings_save(); }   // 松手存 NVS

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
    lv_obj_add_event_cb(sl, chg, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(sl, on_released, LV_EVENT_RELEASED, NULL);
    return sl;
}

static void settings_enter(lv_obj_t *parent) {
    char b[8];
    mk_row(parent, "brightness", 124, SETTINGS_BRIGHT_MIN, 0xFF, settings_brightness(), br_changed, &g_br_val);
    snprintf(b, sizeof b, "%d%%", settings_brightness() * 100 / 255);
    lv_label_set_text(g_br_val, b);

    mk_row(parent, "volume", 244, 0, 100, settings_volume(), vol_changed, &g_vol_val);
    snprintf(b, sizeof b, "%d%%", settings_volume());
    lv_label_set_text(g_vol_val, b);
}

static void settings_exit(void) { g_br_val = g_vol_val = NULL; }

const app_t app_settings = { "settings", COL_TXT, settings_enter, NULL, settings_exit };
