#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "src/display/lv_display_private.h"
#include "glyph.h"
#include "ui_update.h"
#include "app.h"

int64_t host_time_us;
static float host_tx, host_ty;
bool imu_init(void) { return true; }
bool imu_read_tilt(float *x, float *y) { *x = host_tx; *y = host_ty; return true; }
const lv_font_t *i18n_font_m(void) { return LV_FONT_DEFAULT; }
const char *tr(str_id_t id) { (void)id; return "no sensor"; }
// 编译真实流体实现;仅替换硬件时钟、IMU、内存和 PM 锁。
#include "../../main/app_fluid.c"

#define W 466
static uint16_t draw_buffer[W * W], pixels[W * W], expected[W * W];
static lv_display_t *display;
static void flush(lv_display_t *d, const lv_area_t *area, uint8_t *map) {
    int width = lv_area_get_width(area);
    for (int y = area->y1; y <= area->y2; y++) {
        memcpy(pixels + y * W + area->x1, map, width * 2); map += width * 2;
    }
    lv_display_flush_ready(d);
}
static lv_obj_t *root(void) {
    lv_obj_t *o = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(o); lv_obj_set_size(o, W, W);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}
static void old_digits(lv_obj_t *p, const char *text, int pitch, int radius) {
    int x = 71;
    for (unsigned k = 0; text[k]; k++) {
        char c = text[k];
        for (int r = 0; r < 7; r++) for (int col = 0; col < 5; col++) {
            bool dot = c >= '0' && c <= '9' && glyph_font5x7[c - '0'][r][col] == '1';
            if (c == ':') dot = col == 0 && (r == 2 || r == 4);
            if (dot) glyph_dot(p, x + col * pitch + pitch / 2, 168 + r * pitch + pitch / 2,
                               radius, c == ':' ? COL_RED : COL_TXT);
        }
        x += (c >= '0' && c <= '9' ? 6 : 2) * pitch;
    }
}
static void digits_regressions(void) {
    const int pitches[] = {9, 10, 13, 22}, radii[] = {3, 4, 5, 8};
    int cases = 0;
    for (int size = 0; size < 4; size++) for (int digit = 0; digit < 10; digit++) for (int fade = 0; fade < 2; fade++) {
        char text[6]; snprintf(text, sizeof text, "%d%d:%d%d", digit, (digit + 1) % 10, (digit + 2) % 10, (digit + 3) % 10);
        if (size == 3) text[2] = 0; // Bold 表盘每行只有两位,避免测到屏外字符
        lv_obj_t *p = root(); lv_obj_set_style_opa(p, fade ? LV_OPA_50 : LV_OPA_COVER, 0);
        old_digits(p, text, pitches[size], radii[size]); lv_refr_now(display);
        memcpy(expected, pixels, sizeof expected); lv_obj_delete(p);
        p = root(); lv_obj_set_style_opa(p, fade ? LV_OPA_50 : LV_OPA_COVER, 0);
        lv_obj_t *o = glyph_digits_create(p, pitches[size], radii[size]); lv_obj_set_pos(o, 71, 168);
        glyph_digits_set(o, text, COL_TXT, COL_RED); lv_refr_now(display);
        if (memcmp(expected, pixels, sizeof pixels)) {
            fprintf(stderr, "dot pixels differ: pitch=%d digit=%d fade=%d\n", pitches[size], digit, fade); abort();
        }
        uint32_t before = display->inv_p;
        glyph_digits_set(o, text, COL_TXT, COL_RED);
        assert(display->inv_p == before); // 相同数字不重绘
        size_t last = strlen(text) - 1;
        text[last] = text[last] == '9' ? '0' : text[last] + 1;
        glyph_digits_set(o, text, COL_TXT, COL_RED);
        assert(display->inv_p > before);
        assert(lv_area_get_width(&display->inv_areas[before]) <= 5 * pitches[size]); // 单字更新只影响该字
        lv_refr_now(display); memcpy(expected, pixels, sizeof expected);
        lv_obj_delete(p); p = root(); lv_obj_set_style_opa(p, fade ? LV_OPA_50 : LV_OPA_COVER, 0);
        old_digits(p, text, pitches[size], radii[size]); lv_refr_now(display);
        assert(!memcmp(expected, pixels, sizeof pixels)); // 局部更新不能留下灭点残影
        lv_obj_delete(p); cases++;
    }
    printf("dot appearance and partial invalidation: %d cases passed\n", cases);
}
static void style_regressions(void) {
    lv_obj_t *o = glyph_dot(lv_screen_active(), 233, 233, 3, COL_TXT);
    lv_refr_now(display); assert(display->inv_p == 0);
    for (int i = 0; i < 54; i++) { ui_bg_color(o, COL_TXT); ui_bg_opa(o, LV_OPA_COVER); }
    assert(display->inv_p == 0);
    ui_bg_color(o, COL_RED); assert(display->inv_p > 0); lv_obj_delete(o);
    puts("unchanged styles: zero invalidations");
}
static void fluid_regressions(void) {
    lv_obj_t *p = root(); host_time_us = 1000000; host_tx = host_ty = 0;
    fluid_enter(p); assert(g_timer && g_pm_held);
    g_asleep = true; g_ltx = g_lty = 0; pm_hold(false);
    for (int i = 1; i <= 10; i++) {
        host_tx = 0.006f * i; host_time_us += 80000; fluid_frame(g_timer);
    }
    assert(!g_asleep && g_pm_held); // 慢速累计倾斜 0.06g 必须醒
    fluid_visibility(false); assert(!g_pm_held && lv_timer_get_paused(g_timer));
    host_time_us += 10000000; fluid_visibility(true);
    assert(g_pm_held && g_last_us == host_time_us && g_remainder == 0);
    // 真实渲染的所有脏带必须覆盖被修改的像素,包括静止粒子的重叠修补。
    uint16_t *previous = malloc(BW * BW * 2);
    for (int frame = 0; frame < 40; frame++) {
        memcpy(previous, g_buf, BW * BW * 2); host_time_us += 20000; fluid_frame(g_timer);
        for (int y = 0; y < BW; y++) for (int x = 0; x < BW; x++) {
            if (previous[y * BW + x] == g_buf[y * BW + x]) continue;
            lv_area_t *a = &g_dirty[y / BAND_H];
            assert(x >= a->x1 && x <= a->x2 && y >= a->y1 && y <= a->y2);
        }
    }
    free(previous); fluid_exit(); lv_obj_delete(p);
    uint32_t rem_a = 0, rem_b = 0; unsigned a = 0, b = 0;
    for (int i = 0; i < 100; i++) a += motion_steps(&rem_a, 33000);
    for (int i = 0; i < 165; i++) b += motion_steps(&rem_b, 20000);
    assert(a == 400 && a == b && rem_a == rem_b);
    assert(motion_steps(&rem_a, 2000000) == 12); // 忙帧补步有界
    puts("fluid: slow wake, PM/visibility, dirty pixel coverage, equal-time steps passed");
}
void countdown_regressions(void);
int main(void) {
    lv_init(); display = lv_display_create(W, W);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, draw_buffer, NULL, sizeof draw_buffer, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    lv_obj_t *screen = lv_screen_active(); lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0); lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    digits_regressions(); style_regressions(); fluid_regressions(); countdown_regressions(); lv_deinit();
    return 0;
}
