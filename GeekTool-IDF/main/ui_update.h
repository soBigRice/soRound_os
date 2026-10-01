#pragma once
#include "lvgl.h"
#include <string.h>

// LVGL 的样式 setter 即使值相同也会失效化对象;只提交真正变化的值。
static inline void ui_text(lv_obj_t *o, const char *text) {
    if (o && strcmp(lv_label_get_text(o), text)) lv_label_set_text(o, text);
}
static inline void ui_bg_color(lv_obj_t *o, uint32_t color) {
    if (!lv_color_eq(lv_obj_get_style_bg_color(o, 0), lv_color_hex(color)))
        lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
}
static inline void ui_bg_opa(lv_obj_t *o, lv_opa_t opa) {
    if (lv_obj_get_style_bg_opa(o, 0) != opa) lv_obj_set_style_bg_opa(o, opa, 0);
}
