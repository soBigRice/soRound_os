#pragma once
// 点描图标通用画法:沿 圆/弧/线 均匀撒小圆点(Nothing 点描轮廓风)。
// launcher 的 app 图标、天气 app 的天气图标共用。
#include "lvgl.h"

lv_obj_t *glyph_dot(lv_obj_t *par, int x, int y, int r, uint32_t color);
void glyph_arc(lv_obj_t *par, int cx, int cy, int r, float a0, float a1, int step, int dotr, uint32_t color);
void glyph_line(lv_obj_t *par, int x0, int y0, int x1, int y1, int step, int dotr, uint32_t color);
void glyph_circle(lv_obj_t *par, int cx, int cy, int r, int step, int dotr, uint32_t color);
