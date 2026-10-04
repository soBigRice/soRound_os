// Actual LVGL page and renderer; only random input, IMU and NVS are substituted.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "settings.h"
#include "src/misc/lv_text_private.h"
#include "../host/tools_render.h"

static uint8_t language, mode = 3;
static bool sensor_up = true;
static float accel_x;
static int saves, random_calls;
static uint32_t random_value;
uint8_t settings_lang(void) { return language; }
uint8_t settings_dice(void) { return mode; }
void settings_set_dice(uint8_t value) { mode = value; }
void settings_save(void) { ++saves; }
bool imu_init(void) { return sensor_up; }
bool imu_read_accel(float *x, float *y, float *z) {
    *x = accel_x; *y = 0; *z = 1; return sensor_up;
}
uint32_t esp_random(void) { ++random_calls; return random_value; }
static lv_obj_t *heading;
void launcher_set_title(const char *text) { lv_label_set_text(heading, text); }
#include "../../main/app_dice.c"

static uint16_t buffer[466 * 466], pixels[466 * 466];
static lv_display_t *display;
static unsigned flushed_pixels;
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *map) {
    int width = lv_area_get_width(a);
    flushed_pixels += (unsigned)(width * lv_area_get_height(a));
    for (int y = a->y1; y <= a->y2; ++y) {
        memcpy(pixels + y * 466 + a->x1, map, (size_t)width * 2); map += width * 2;
    }
    lv_display_flush_ready(d);
}
static bool has_glyphs(lv_obj_t *label) {
    const char *text = lv_label_get_text(label);
    const lv_font_t *font = lv_obj_get_style_text_font(label, 0);
    uint32_t at = 0;
    while (text[at]) {
        uint32_t cp = lv_text_encoded_next(text, &at);
        lv_font_glyph_dsc_t glyph;
        if (!lv_font_get_glyph_dsc(font, &glyph, cp, 0) || glyph.is_placeholder) return false;
    }
    return true;
}
static void check_labels(lv_obj_t *obj) {
    if (ui_obj_is_hidden(obj)) return;
    if (lv_obj_check_type(obj, &lv_label_class)) {
        assert(has_glyphs(obj));
        lv_area_t a; lv_obj_get_coords(obj, &a);
        lv_obj_get_transformed_area(obj, &a, LV_OBJ_POINT_TRANSFORM_FLAG_RECURSIVE);
        for (int i = 0; i < 4; ++i) {
            int x = i & 1 ? a.x2 : a.x1, y = i & 2 ? a.y2 : a.y1;
            if (hypot(x - 232.5, y - 232.5) > 225)
                fprintf(stderr, "label outside circle: %s (%d,%d)-(%d,%d)\n", lv_label_get_text(obj), a.x1, a.y1, a.x2, a.y2);
            assert(hypot(x - 232.5, y - 232.5) <= 225);
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i) check_labels(lv_obj_get_child(obj, i));
}
static void capture(const char *dir, const char *name) {
    lv_obj_update_layout(lv_screen_active()); lv_obj_update_layout(lv_layer_top());
    check_labels(lv_screen_active()); check_labels(lv_layer_top());
    lv_refr_now(display);
    if (!dir) return;
    char path[512]; snprintf(path, sizeof path, "%s/coin-%s-%s.ppm", dir, name, language ? "zh" : "en");
    FILE *f = fopen(path, "wb"); assert(f); fprintf(f, "P6\n466 466\n255\n");
    for (int i = 0; i < 466 * 466; ++i) {
        uint16_t p = pixels[i];
        uint8_t rgb[] = {((p >> 11) & 31) * 255 / 31, ((p >> 5) & 63) * 255 / 63, (p & 31) * 255 / 31};
        fwrite(rgb, 1, 3, f);
    }
    assert(fclose(f) == 0);
}
static void step(void) { lv_tick_inc(20); dice_tick(); }
static lv_obj_t *enter_page(void) {
    lv_obj_t *page = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(page); lv_obj_set_size(page, 466, 466);
    dice_enter(page); return page;
}
static void tap(void) { lv_obj_send_event(lv_obj_get_child(g_main, 3), LV_EVENT_CLICKED, NULL); }
int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : NULL;
    lv_init(); i18n_init();
    display = lv_display_create(466, 466); lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, buffer, NULL, sizeof buffer, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0), 0);
    heading = lv_label_create(lv_layer_top());
    lv_obj_set_style_text_font(heading, UI_FONT_L, 0);
    lv_obj_set_style_text_color(heading, lv_color_hex(COL_TXT), 0);
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 46);
    tools_test_battery();
    lv_obj_t *back = lv_obj_create(lv_layer_top()), *arrow = lv_label_create(back);
    lv_obj_set_size(back, 48, 48); lv_obj_align(back, LV_ALIGN_TOP_MID, -100, 40);
    lv_obj_set_style_radius(back, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_pad_all(back, 0, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x16161a), 0);
    lv_obj_set_style_border_width(back, 1, 0); lv_obj_set_style_border_color(back, lv_color_hex(COL_TXT2), 0);
    lv_obj_set_style_text_font(arrow, UI_FONT_SYM, 0); lv_label_set_text(arrow, LV_SYMBOL_LEFT); lv_obj_center(arrow);
    lv_obj_set_style_text_color(arrow, lv_color_hex(COL_TXT), 0);
    for (language = 0; language < 2; ++language) {
        mode = M_COIN; sensor_up = true; accel_x = 0;
        lv_obj_t *page = enter_page();
        capture(dir, "ready");
        assert(has_glyphs(g_coinlbl)); // Old Montserrat-40 page failed on the Chinese mark.
        for (random_value = 0; random_value < 2; ++random_value) {
            int calls = random_calls;
            tap(); assert(s_coin_toss && random_calls == calls + 1);
            tap(); assert(random_calls == calls + 1); // Rapid taps cannot reroll a pending outcome.
            int rest_y = COIN_Y - COIN_D / 2, min_y = rest_y, min_scale = 256, flips = 0, previous = s_coin_side;
            for (int i = 0; i < 62; ++i) {
                step();
                if (s_coin_side != previous) { ++flips; previous = s_coin_side; }
                int y = lv_obj_get_y(g_coin), scale = lv_obj_get_style_transform_scale_y(g_coin, 0);
                if (y < min_y) min_y = y;
                if (scale < min_scale) min_scale = scale;
                char name[32]; snprintf(name, sizeof name, "toss-%u-%02d", random_value, i);
                capture(language == 1 && random_value == 1 ? dir : NULL, name);
            }
            assert(!s_coin_toss && s_val[0] == (int)random_value && s_coin_side == (int)random_value);
            assert(random_calls == calls + 1 && min_y <= rest_y - 50 && min_scale < 32 && flips >= 6);
            assert(lv_obj_get_y(g_coin) == rest_y && lv_obj_get_style_transform_scale_y(g_coin, 0) == 256);
            assert(!strcmp(lv_label_get_text(g_sum), tr(random_value ? S_HEADS : S_TAILS)));
            capture(dir, random_value ? "heads" : "tails");
        }
        if (language == 1 && dir) {
            random_value = 0; tap();
            for (int i = 0; i < 62; ++i) {
                step(); char name[32]; snprintf(name, sizeof name, "return-%02d", i); capture(dir, name);
            }
            assert(s_val[0] == 0 && !s_coin_toss);
        }
        // Covered pages pause the exact toss phase and discard stale sensor deltas on resume.
        tap(); for (int i = 0; i < 15; ++i) step();
        uint32_t elapsed = s_coin_elapsed;
        dice_visibility(false); lv_tick_inc(2000); accel_x = 4; dice_visibility(true); step();
        assert(s_coin_elapsed == elapsed + 20);
        lv_obj_send_event(lv_obj_get_child(g_main, 4), LV_EVENT_CLICKED, NULL);
        assert(s_view == 1 && !s_coin_toss); capture(dir, "settings");
        tap(); assert(!s_coin_toss);
        int before = saves;
        lv_obj_t *list = lv_obj_get_child(g_set, 1);
        lv_obj_send_event(lv_obj_get_child(list, 0), LV_EVENT_CLICKED, NULL);
        assert(mode == 0 && saves == before + 1 && dice_back());
        assert(!g_coin && lv_obj_get_style_text_font(g_sum, 0) == UI_FONT_SYM);
        capture(dir, "one-die");
        dice_exit(); lv_obj_delete(page); step(); assert(!g_coin && !s_coin_toss);
        // Returning directly to coin mode without an IMU still provides working taps and a hint.
        mode = M_COIN; sensor_up = false; page = enter_page(); capture(dir, "tap-only");
        assert(!strcmp(lv_label_get_text(g_hint), language ? tr(S_DICE_TAP) : "TAP TO TOSS"));
        tap(); for (int i = 0; i < 62; ++i) step(); assert(!s_coin_toss);
        dice_exit(); lv_obj_delete(page);
        // Shake is still wired through the real polling branch; settled pages perform no redraw.
        sensor_up = true; accel_x = 0; page = enter_page();
        for (int i = 0; i < 3; ++i) step();
        accel_x = 2; for (int i = 0; i < 3; ++i) step(); assert(s_coin_toss);
        for (int i = 0; i < 62; ++i) step(); capture(NULL, "settled");
        flushed_pixels = 0; for (int i = 0; i < 10; ++i) step(); lv_refr_now(display);
        assert(flushed_pixels == 0);
        dice_exit(); lv_obj_delete(page);
        for (mode = 0; mode < 3; ++mode) {
            page = enter_page(); random_value = 5; tap();
            for (int i = 0; i < 55; ++i) step();
            assert(!s_roll);
            int n = mode + 1;
            for (int i = 0; i < n; ++i) assert(s_val[i] == 6);
            char sum[8]; snprintf(sum, sizeof sum, "%d", 6 * n);
            assert(!strcmp(lv_label_get_text(g_sum), sum));
            assert(!strcmp(lv_label_get_text(g_hint), n > 1 ? tr(S_DICE_DOUBLE) : ""));
            char name[16]; snprintf(name, sizeof name, "%d-dice", n); capture(dir, name);
            dice_exit(); lv_obj_delete(page);
        }
    }
    puts("coin: Chinese/English glyphs and round layout, tap/shake, ascent/flips/landing, both outcomes, rapid-tap guard, cover/resume, settings/cancel/exit, IMU fallback, idle no-redraw and 1/2/3-dice regressions passed");
}
