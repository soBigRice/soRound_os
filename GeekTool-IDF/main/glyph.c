// 点描图标通用画法 —— 见 glyph.h。每个点是一个小圆 lv_obj。
#include "glyph.h"
#include <math.h>

#define G_PI 3.14159265f

// 5×7 点阵数字字模 0-9(各表盘/计时 app 共用,原先每文件各存一份,现收敛到此)
const char *const glyph_font5x7[10][7] = {
    {"01110","10001","10011","10101","11001","10001","01110"},
    {"00100","01100","00100","00100","00100","00100","01110"},
    {"01110","10001","00001","00010","00100","01000","11111"},
    {"11110","00001","00001","01110","00001","00001","11110"},
    {"00010","00110","01010","10010","11111","00010","00010"},
    {"11111","10000","11110","00001","00001","10001","01110"},
    {"00110","01000","10000","11110","10001","10001","01110"},
    {"11111","00001","00010","00100","01000","01000","01000"},
    {"01110","10001","10001","01110","10001","10001","01110"},
    {"01110","10001","10001","01111","00001","00010","01100"},
};

lv_obj_t *glyph_dot(lv_obj_t *par, int x, int y, int r, uint32_t color) {
    lv_obj_t *d = lv_obj_create(par);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, r * 2, r * 2);
    lv_obj_set_pos(d, x - r, y - r);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(d, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);   // 关键:remove_style_all 后 bg_opa 默认透明,必须显式置满
    lv_obj_remove_flag(d, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(d, LV_OBJ_FLAG_EVENT_BUBBLE);   // 手势冒泡到父屏
    return d;
}

void glyph_arc(lv_obj_t *par, int cx, int cy, int r, float a0, float a1, int step, int dotr, uint32_t color) {
    int n = (int)(fabsf(a1 - a0) * r / step);
    if (n < 1) n = 1;
    for (int i = 0; i <= n; i++) {
        float a = a0 + (a1 - a0) * i / n;
        glyph_dot(par, cx + (int)(cosf(a) * r), cy + (int)(sinf(a) * r), dotr, color);
    }
}

void glyph_line(lv_obj_t *par, int x0, int y0, int x1, int y1, int step, int dotr, uint32_t color) {
    float dist = sqrtf((float)((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)));
    int n = (int)(dist / step);
    if (n < 1) n = 1;
    for (int i = 0; i <= n; i++)
        glyph_dot(par, x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n, dotr, color);
}

void glyph_circle(lv_obj_t *par, int cx, int cy, int r, int step, int dotr, uint32_t color) {
    int n = (int)(2 * G_PI * r / step);
    if (n < 4) n = 4;
    for (int i = 0; i < n; i++) {
        float a = (float)i / n * 2 * G_PI;
        glyph_dot(par, cx + (int)(cosf(a) * r), cy + (int)(sinf(a) * r), dotr, color);
    }
}
