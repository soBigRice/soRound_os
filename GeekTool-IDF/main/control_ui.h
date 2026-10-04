#pragma once
#include "app.h"
#include "lvgl_compat.h"
#include "weather_location_ui.h"

// Settings and Wi-Fi share these controls; other apps keep their existing theme.
#define CONTROL_WHITE 0xf5f5f2
#define CONTROL_GRAY  0xa0a0a6
#define CONTROL_CARD  0x1b1b1f
#define CONTROL_LINE  0x424248
LV_FONT_DECLARE(font_control_18);
const lv_font_t *control_small_font(void);
lv_obj_t *control_surface(lv_obj_t *parent, int x, int y, int width, int height);
lv_obj_t *control_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                        int x, int y, int width, uint32_t color);
lv_obj_t *control_button(lv_obj_t *parent, int x, int y, int width, int height,
                         lv_event_cb_t callback, void *data);
void control_button_text(lv_obj_t *button, const char *text);
lv_obj_t *control_slider(lv_obj_t *parent, int y, int min, int max, int value,
                         lv_event_cb_t changed, lv_event_cb_t released);
lv_obj_t *control_toggle(lv_obj_t *parent, int x, int y, int width,
                         const char *title, const char *detail, bool on,
                         lv_event_cb_t changed, void *data);
