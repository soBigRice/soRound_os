#pragma once
#include "app.h"
#include "lvgl_compat.h"

// Palette and typography belong only to the approved audio / level screens.
#define TOOLS_WHITE 0xf5f5f2
#define TOOLS_GRAY  0x929294
#define TOOLS_DIM   0x242426
#define TOOLS_LINE  0x353538
#define TOOLS_FAINT 0x171719
LV_FONT_DECLARE(font_tools_19);
LV_FONT_DECLARE(font_tools_20);
LV_FONT_DECLARE(font_tools_21);
LV_FONT_DECLARE(font_tools_22);
LV_FONT_DECLARE(font_tools_24);
LV_FONT_DECLARE(font_tools_36);
LV_FONT_DECLARE(font_tools_60);

const char *tools_text(const char *en, const char *zh);
lv_obj_t *tools_surface(lv_obj_t *parent, int x, int y, int w, int h);
lv_obj_t *tools_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                      int cx, int cy, uint32_t color, int spacing);
void tools_label_center(lv_obj_t *label, int cx, int cy);
void tools_label_baseline(lv_obj_t *label, int x, int baseline);
void tools_dot(lv_layer_t *layer, int x, int y, int diameter, uint32_t color);
void tools_circle(lv_layer_t *layer, int x, int y, int radius, int width, uint32_t color);
void tools_line(lv_layer_t *layer, int x1, int y1, int x2, int y2, int width, uint32_t color);
lv_obj_t *tools_fault(lv_obj_t *parent, bool audio);
void tools_header(lv_obj_t *title, lv_obj_t *back, lv_obj_t *arrow);
