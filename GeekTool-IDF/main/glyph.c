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

#include "src/core/lv_obj_private.h"
#include "src/core/lv_obj_class_private.h"
#include <string.h>

typedef struct {
    lv_obj_t obj;
    char text[8];
    int pitch, radius;
    uint32_t color, colon_color;
} digits_t;

static void digits_event(const lv_obj_class_t *cls, lv_event_t *e);
static const lv_obj_class_t digits_class = {
    .base_class = &lv_obj_class, .event_cb = digits_event,
    .instance_size = sizeof(digits_t), .name = "glyph_digits",
};

static int char_width(char c, int pitch) { return (c >= '0' && c <= '9' ? 5 : 1) * pitch; }

static void digits_event(const lv_obj_class_t *cls, lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_COVER_CHECK) {
        // 字符之间和灭点都是透明的,不能让刷新器误认为整个矩形遮住了背景。
        lv_event_set_cover_res(e, LV_COVER_RES_NOT_COVER);
        return;
    }
    if (code != LV_EVENT_DRAW_MAIN && code != LV_EVENT_DRAW_MAIN_END) {
        lv_obj_event_base(cls, e);
        return;
    }
    if (code != LV_EVENT_DRAW_MAIN) return;
    lv_obj_t *o = lv_event_get_target_obj(e);
    digits_t *d = (digits_t *)o;
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.base.layer = layer;
    lv_obj_init_draw_rect_dsc(o, LV_PART_MAIN, &rect); // 遵循 LVGL 的父层透明度
    lv_area_t coords;
    lv_obj_get_coords(o, &coords);
    int x = coords.x1;
    for (unsigned k = 0; d->text[k]; k++) {
        char c = d->text[k];
        rect.bg_color = lv_color_hex(c == ':' ? d->colon_color : d->color);
        for (int row = 0; row < 7; row++) {
            for (int col = 0; col < 5; col++) {
                bool dot = c >= '0' && c <= '9' && glyph_font5x7[c - '0'][row][col] == '1';
                if (c == ':') dot = col == 0 && (row == 2 || row == 4);
                if (!dot) continue;
                int cx = x + col * d->pitch + d->pitch / 2;
                int cy = coords.y1 + row * d->pitch + d->pitch / 2;
                lv_area_t a = {cx - d->radius, cy - d->radius,
                               cx + d->radius - 1, cy + d->radius - 1};
                lv_draw_rect(layer, &rect, &a);
            }
        }
        x += char_width(c, d->pitch) + d->pitch;
    }
}

lv_obj_t *glyph_digits_create(lv_obj_t *parent, int pitch, int radius) {
    lv_obj_t *o = lv_obj_class_create_obj(&digits_class, parent);
    if (!o) return NULL;
    lv_obj_class_init_obj(o);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    digits_t *d = (digits_t *)o;
    d->pitch = pitch; d->radius = radius;
    return o;
}

void glyph_digits_set(lv_obj_t *o, const char *text, uint32_t color, uint32_t colon_color) {
    if (!o) return;
    digits_t *d = (digits_t *)o;
    size_t n = strlen(text);
    if (n >= sizeof d->text) return;
    if (!strcmp(d->text, text) && d->color == color && d->colon_color == colon_color) return;
    int width = 0, old_width = lv_obj_get_width(o);
    for (size_t k = 0; k < n; k++) width += char_width(text[k], d->pitch) + (k ? d->pitch : 0);
    if (width != old_width || strlen(d->text) != n) {
        lv_obj_set_size(o, width, 7 * d->pitch);
        lv_obj_invalidate(o);
    } else {
        lv_area_t coords;
        lv_obj_get_coords(o, &coords);
        int x = coords.x1;
        for (size_t k = 0; k < n; k++) {
            int w = char_width(text[k], d->pitch);
            uint32_t old_col = d->text[k] == ':' ? d->colon_color : d->color;
            uint32_t new_col = text[k] == ':' ? colon_color : color;
            if (text[k] != d->text[k] || old_col != new_col) {
                lv_area_t a = {x, coords.y1, x + w - 1, coords.y2};
                lv_obj_invalidate_area(o, &a);
            }
            x += w + d->pitch;
        }
    }
    memcpy(d->text, text, n + 1);
    d->color = color; d->colon_color = colon_color;
}
