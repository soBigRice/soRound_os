// Real LVGL and pixel controller. Clock, acceleration and hardware allocation
// are fixtures; actual pointer events drive both the field and its buttons.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "settings.h"
#include "esp_heap_caps.h"
#include "esp_pm.h"
#include "src/misc/lv_text_private.h"

#define W 466
LV_FONT_DECLARE(font_location_24);
int64_t host_time_us = 1000000;
static uint8_t language;
static bool sensor_present = true, sensor_reads = true;
static float accel_x, accel_y, accel_z = 1;
static unsigned acceleration_reads, allocations, live, fail_at;
static void *owned[16];
static lv_obj_t *heading, *back_button, *page;
static lv_display_t *display;
static lv_indev_t *input;
static bool controller_active;
static lv_indev_data_t pointer = {.state = LV_INDEV_STATE_RELEASED};
static unsigned flushes, back_actions;
static const char *folder;
static uint16_t framebuffer[W * W];
_Alignas(LV_DRAW_BUF_ALIGN) static uint16_t buffer[W * 48];

uint8_t settings_lang(void) { return language; }
void settings_set_lang(uint8_t value) { language = value; }
void settings_save(void) {}
void launcher_set_title(const char *text) { lv_label_set_text(heading, text); }
bool imu_init(void) { return sensor_present; }
bool imu_read_accel(float *x, float *y, float *z) {
    ++acceleration_reads;
    *x = accel_x; *y = accel_y; *z = accel_z;
    return sensor_present && sensor_reads;
}

static void *test_malloc(size_t size) {
    if(++allocations == fail_at) return NULL;
    void *p = malloc(size); assert(p);
    unsigned slot = 0;
    while(slot < 16 && owned[slot]) ++slot;
    assert(slot < 16); owned[slot] = p; ++live;
    return p;
}
static void test_free(void *p) {
    if(!p) return;
    unsigned slot = 0;
    while(slot < 16 && owned[slot] != p) ++slot;
    assert(slot < 16); owned[slot] = NULL; --live; free(p);
}
static void *test_heap_malloc(size_t size, unsigned caps) {
    (void)caps; return test_malloc(size);
}
#define malloc test_malloc
#define free test_free
#define heap_caps_malloc test_heap_malloc
#define heap_caps_free test_free
#include "../../main/app_pixels.c"
#undef malloc
#undef free
#undef heap_caps_malloc
#undef heap_caps_free

static void flush(lv_display_t *d, const lv_area_t *area, uint8_t *bytes) {
    ++flushes;
    int width = lv_area_get_width(area);
    for(int y = area->y1; y <= area->y2; ++y) {
        assert(area->x1 >= 0 && area->x2 < W && y >= 0 && y < W);
        memcpy(framebuffer + y * W + area->x1, bytes, (size_t)width * 2);
        bytes += width * 2;
    }
    lv_display_flush_ready(d);
}
static void read_pointer(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev; *data = pointer;
}
static void advance(unsigned ms) {
    host_time_us += (int64_t)ms * 1000;
    lv_tick_inc(ms);
    // Launcher owns the heartbeat; the app itself has no independent timer.
    if(controller_active && app_pixels.tick) app_pixels.tick();
    lv_timer_handler();
}
static void run_for(unsigned ms) {
    for(unsigned elapsed = 0; elapsed < ms; elapsed += 20) advance(20);
}
static void touch_at(int x, int y, bool down) {
    pointer.point = (lv_point_t){x, y};
    pointer.state = down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    advance(20);
}
static void tap(int x, int y) {
    touch_at(x, y, true); touch_at(x, y, false); advance(20);
}
static void brush(int x1, int y1, int x2, int y2) {
    touch_at(x1, y1, true);
    for(int step = 1; step <= 16; ++step)
        touch_at(x1 + (x2 - x1) * step / 16,
                 y1 + (y2 - y1) * step / 16, true);
    touch_at(x2, y2, false);
}
static unsigned timer_count(void) {
    unsigned count = 0;
    for(lv_timer_t *timer = lv_timer_get_next(NULL); timer;
        timer = lv_timer_get_next(timer)) ++count;
    return count;
}
static void check_labels(lv_obj_t *object) {
    if(lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) return;
    if(lv_obj_check_type(object, &lv_label_class)) {
        const char *text = lv_label_get_text(object);
        const lv_font_t *font = lv_obj_get_style_text_font(object, 0);
        uint32_t offset = 0;
        while(text[offset]) {
            uint32_t code = lv_text_encoded_next(text, &offset);
            if(code == '\n') continue;
            lv_font_glyph_dsc_t glyph = {0};
            bool valid = lv_font_get_glyph_dsc(font, &glyph, code, 0) && !glyph.is_placeholder;
            if(!valid) fprintf(stderr, "missing pixel-app glyph U+%04X in '%s'\n", (unsigned)code, text);
            assert(valid);
        }
        lv_area_t area; lv_obj_get_coords(object, &area);
        for(unsigned corner = 0; corner < 4; ++corner) {
            int x = (corner & 1) ? area.x2 : area.x1;
            int y = (corner & 2) ? area.y2 : area.y1;
            if(hypot(x - 232.5, y - 232.5) > 225)
                fprintf(stderr, "pixel-app label outside circle: '%s' (%d,%d)\n", text, x, y);
            assert(hypot(x - 232.5, y - 232.5) <= 225);
        }
    }
    for(unsigned child = 0; child < lv_obj_get_child_count(object); ++child)
        check_labels(lv_obj_get_child(object, child));
}
static void capture(const char *state) {
    lv_obj_update_layout(page); lv_obj_update_layout(lv_layer_top());
    lv_refr_now(display); check_labels(page); check_labels(heading); check_labels(back_button);
    if(!folder) return;
    char path[1024];
    snprintf(path, sizeof path, "%s/pixels-%s-%s.ppm", folder, state, language ? "zh" : "en");
    FILE *file = fopen(path, "wb"); assert(file);
    fprintf(file, "P6\n466 466\n255\n");
    for(unsigned index = 0; index < W * W; ++index) {
        uint16_t pixel = framebuffer[index];
        uint8_t rgb[] = {((pixel >> 11) & 31) * 255 / 31,
                         ((pixel >> 5) & 63) * 255 / 63, (pixel & 31) * 255 / 31};
        assert(fwrite(rgb, 1, 3, file) == 3);
    }
    assert(fclose(file) == 0);
}
static void back_clicked(lv_event_t *event) { (void)event; ++back_actions; }
static void new_page(void) {
    page = lv_obj_create(lv_screen_active()); lv_obj_remove_style_all(page);
    lv_obj_set_size(page, W, W); ui_obj_set_scrollable(page, false);
    // Same shared header geometry and fonts used for normal launcher apps.
    heading = lv_label_create(lv_layer_top());
    lv_obj_set_style_text_font(heading, &font_location_24, 0);
    lv_obj_set_style_text_color(heading, lv_color_hex(COL_TXT), 0);
    lv_label_set_text(heading, tr_app_name("Pixels"));
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 46);
    back_button = lv_obj_create(lv_layer_top()); lv_obj_remove_style_all(back_button);
    lv_obj_set_size(back_button, 48, 48); lv_obj_align(back_button, LV_ALIGN_TOP_MID, -100, 40);
    lv_obj_set_style_radius(back_button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(back_button, lv_color_hex(0x16161a), 0);
    lv_obj_set_style_bg_opa(back_button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(back_button, 1, 0);
    lv_obj_set_style_border_color(back_button, lv_color_hex(COL_TXT2), 0);
    lv_obj_set_style_border_opa(back_button, LV_OPA_50, 0);
    ui_obj_set_scrollable(back_button, false); ui_obj_set_clickable(back_button, true);
    lv_obj_add_event_cb(back_button, back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *arrow = lv_label_create(back_button); lv_label_set_text(arrow, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(arrow, UI_FONT_SYM, 0);
    lv_obj_set_style_text_color(arrow, lv_color_hex(COL_TXT), 0); lv_obj_center(arrow);
    app_pixels.enter(page); controller_active = true; advance(20);
}
static void close_page(void) {
    controller_active = false; app_pixels.exit();
    lv_obj_delete(page); lv_obj_delete(heading); lv_obj_delete(back_button);
    page = heading = back_button = NULL; pointer.state = LV_INDEV_STATE_RELEASED;
    advance(20); assert(live == 0);
}
static void delete_page_without_exit(void) {
    // Cover the real body's delete callback if its parent disappears first.
    controller_active = false; lv_obj_delete(page);
    lv_obj_delete(heading); lv_obj_delete(back_button);
    page = heading = back_button = NULL; pointer.state = LV_INDEV_STATE_RELEASED;
    advance(20); assert(live == 0);
}

static uint32_t raster_hash(const pixel_field_t *field) {
    uint32_t hash = 2166136261u;
    for(unsigned i = 0; i < field->count; ++i) {
        const pixel_field_tile_t *tile = &field->tiles[i];
        if(tile->color < 0) continue;
        hash = (hash ^ (uint32_t)(int32_t)lroundf(tile->x)) * 16777619u;
        hash = (hash ^ (uint32_t)(int32_t)lroundf(tile->y)) * 16777619u;
        hash = (hash ^ (uint32_t)tile->color) * 16777619u;
    }
    return hash;
}
static void check_field(const pixel_field_t *field) {
    assert(field->count > 100 && field->count <= PIXEL_FIELD_MAX_TILES);
    for(unsigned i = 0; i < field->count; ++i) {
        const pixel_field_tile_t *tile = &field->tiles[i];
        assert(isfinite(tile->x) && isfinite(tile->y) && isfinite(tile->vx) && isfinite(tile->vy));
        assert(tile->color >= -1 && tile->color < 6);
        assert(tile->x >= 0 && tile->x < W && tile->y >= 0 && tile->y < W);
    }
}
static void settle(void) {
    unsigned elapsed = 0;
    while(s_field->active && elapsed < 10000) { advance(20); elapsed += 20; check_field(s_field); }
    assert(!s_field->active && !s_field->touching && !s_pm_held);
    run_for(100); lv_refr_now(display);
}
static void input_acceleration(float x, float y, float z, unsigned ms) {
    accel_x = x; accel_y = y; accel_z = z; run_for(ms);
}
static void shake_pulses(void) {
    input_acceleration(2.0f, 0, 0, 60);
    input_acceleration(0, 0, 1, 60);
    input_acceleration(-2.0f, 0, 0, 60);
}
static void shake_detector_boundaries(void) {
    pixel_shake_t detector;
    pixel_shake_reset(&detector);
    // Multiple samples from one continuous impulse are not distinct peaks.
    for(uint32_t at = 1000; at <= 1500; at += 20)
        assert(!pixel_shake_sample(&detector, 2, 0, 0, at));
    assert(!detector.blocked);
    pixel_shake_reset(&detector);
    assert(!pixel_shake_sample(&detector, 2, 0, 0, 2000));
    assert(!pixel_shake_sample(&detector, NAN, 0, 1, 2040));
    assert(!pixel_shake_sample(&detector, -2, 0, 0, 2080));
    assert(!detector.blocked);
    pixel_shake_reset(&detector);
    assert(!pixel_shake_sample(&detector, 2, 0, 0, 2200));
    assert(!pixel_shake_sample(&detector, 10, 0, 0, 2240));
    assert(!pixel_shake_sample(&detector, -2, 0, 0, 2280));
    assert(!detector.blocked);
    pixel_shake_reset(&detector);
    assert(!pixel_shake_sample(&detector, NAN, 0, 1, 100));
    assert(!pixel_shake_sample(&detector, 0, INFINITY, 1, 200));
    assert(!pixel_shake_sample(&detector, 10, 0, 1, 250));
    assert(!pixel_shake_sample(&detector, 0, 0, 1, 300));
    assert(!pixel_shake_sample(&detector, 2, 0, 0, 400));
    assert(!pixel_shake_sample(&detector, 0, 0, 1, 420));
    assert(pixel_shake_sample(&detector, -2, 0, 0, 460));
    for(uint32_t at = 510; at <= 810; at += 50)
        assert(!pixel_shake_sample(&detector, 0, 0, 1, at));
    assert(detector.blocked); // Quiet alone cannot bypass the 900ms cooldown.
    for(uint32_t at = 860; at <= 1810; at += 50)
        assert(!pixel_shake_sample(&detector, 2, 0, 0, at));
    // Cooldown alone does not rearm without 300ms of stable gravity.
    assert(detector.blocked);
    for(uint32_t at = 1860; at <= 2160; at += 50)
        assert(!pixel_shake_sample(&detector, 0, 0, 1, at));
    assert(!pixel_shake_sample(&detector, 2, 0, 0, 2210));
    assert(!pixel_shake_sample(&detector, 0, 0, 1, 2240));
    assert(pixel_shake_sample(&detector, -2, 0, 0, 2270));
    // A lone impulse that expires cannot combine with a much later one.
    pixel_shake_reset(&detector);
    assert(!pixel_shake_sample(&detector, 2, 0, 0, 3000));
    assert(!pixel_shake_sample(&detector, 0, 0, 1, 3100));
    assert(!pixel_shake_sample(&detector, -2, 0, 0, 3300));
    assert(!pixel_shake_sample(&detector, 0, 0, 1, 3340));
    assert(pixel_shake_sample(&detector, 2, 0, 0, 3380));
    pixel_shake_reset(&detector);
    assert(!pixel_shake_sample(&detector, 2, 0, 0, UINT32_MAX - 30));
    assert(!pixel_shake_sample(&detector, 0, 0, 1, UINT32_MAX));
    assert(pixel_shake_sample(&detector, -2, 0, 0, 29));
    puts("Shake detector: sustained single impulse rejected, valid peak/low/peak, broken sample rejection, expired impulse, cooldown and quiet rearm passed.");
}
static void field_input_boundaries(void) {
    struct { uint32_t before[4]; pixel_field_t field; uint32_t after[4]; } guarded;
    for(unsigned index = 0; index < 4; ++index) guarded.before[index] = guarded.after[index] = 0x5aa5f00du;
    for(unsigned pattern = 0; pattern < PIXEL_FIELD_PATTERNS; ++pattern) {
        pixel_field_init(&guarded.field, pattern); check_field(&guarded.field);
        pixel_field_t original = guarded.field;
        pixel_field_touch(&guarded.field, NAN, 230, true);
        pixel_field_touch(&guarded.field, 230, INFINITY, true);
        pixel_field_touch(&guarded.field, -100, 230, true);
        assert(!memcmp(&original, &guarded.field, sizeof original));
        assert(!pixel_field_step(&guarded.field, 0));
        pixel_field_touch(&guarded.field, 233, 233, true);
        assert(guarded.field.touching && guarded.field.active);
        pixel_field_touch(&guarded.field, 233, 233, false);
        original = guarded.field;
        assert(!pixel_field_step(&guarded.field, NAN));
        assert(!pixel_field_step(&guarded.field, -1));
        assert(!memcmp(&original, &guarded.field, sizeof original));
        pixel_field_repattern(&guarded.field, pattern, true);
        for(unsigned step = 0; step < 400; ++step) {
            pixel_field_step(&guarded.field, step % 2 ? .02f : 10);
            check_field(&guarded.field);
        }
        assert(!guarded.field.active);
        for(unsigned index = 0; index < 4; ++index)
            assert(guarded.before[index] == 0x5aa5f00du && guarded.after[index] == 0x5aa5f00du);
    }
    puts("Pixel field: all patterns, guarded bounds, invalid input/elapsed time, bounded scattering and convergence passed.");
}
static void font_history_coverage(void) {
    // Font regeneration must preserve these historical glyphs as well as the new title.
    const uint32_t codes[] = {0x6548, 0x751f, 0x5757}; // 效 / 生 / 块
    for(unsigned index = 0; index < sizeof codes / sizeof codes[0]; ++index) {
        lv_font_glyph_dsc_t glyph = {0};
        assert(lv_font_get_glyph_dsc(&font_location_24, &glyph, codes[index], 0));
        assert(!glyph.is_placeholder);
    }
}
static void interaction_and_lifecycle(void) {
    unsigned baseline_timers = timer_count();
    for(language = 0; language < 2; ++language) {
        // Independent fresh-process defaults make EN/ZH review frames comparable.
        // Interactions below use actual pointer/IMU input, never edited tile positions.
        s_palette = s_pattern = 0;
        i18n_init(); sensor_present = sensor_reads = true; accel_x = accel_y = 0; accel_z = 1;
        new_page(); assert(s_field && s_body && s_has_imu && live == 1);
        assert(app_pixels.tick_period_ms == 20 && timer_count() == baseline_timers);
        check_field(s_field); capture("initial");
        uint32_t initial = raster_hash(s_field);
        brush(146, 231, 320, 230); check_field(s_field);
        assert(raster_hash(s_field) != initial && !s_field->touching && s_pm_held);
        capture("touch");
        unsigned palette = s_palette;
        tap(185, 397); assert(s_palette == (palette + 1) % PIXEL_FIELD_PALETTES);
        capture("palette");
        tap(281, 397); settle(); capture("regroup");
        for(unsigned i = 0; i < s_field->count; ++i) {
            assert(fabsf(s_field->tiles[i].x - s_field->tiles[i].tx) < 1);
            assert(fabsf(s_field->tiles[i].y - s_field->tiles[i].ty) < 1);
        }
        unsigned before_flush = flushes, before_reads = acceleration_reads;
        run_for(1500); assert(flushes == before_flush && acceleration_reads > before_reads);
        // Actual drag is still available after the settled app stops drawing.
        brush(185, 215, 295, 265); assert(s_field->active && s_pm_held && flushes > before_flush);
        app_pixels.visibility(false);
        assert(!s_pm_held);
        pixel_field_t snapshot = *s_field;
        before_reads = acceleration_reads; before_flush = flushes;
        input_acceleration(2.0f, 0, 0, 6000);
        assert(!s_visible && !memcmp(&snapshot, s_field, sizeof snapshot));
        assert(acceleration_reads == before_reads && flushes == before_flush);
        accel_x = accel_y = 0; accel_z = 1;
        app_pixels.visibility(true); assert(s_visible && !s_shake.blocked && s_pm_held);
        assert(raster_hash(s_field) == raster_hash(&snapshot));
        run_for(100); assert(raster_hash(s_field) != raster_hash(&snapshot));
        tap(281, 397); settle();
        unsigned pattern = s_pattern;
        input_acceleration(2.0f, 0, 0, 1000);
        assert(s_pattern == pattern && !s_shake.blocked);
        input_acceleration(0, 0, 1, 400);
        input_acceleration(2.0f, 0, 0, 60);
        input_acceleration(0, 0, 1, 60);
        sensor_reads = false; input_acceleration(0, 0, 1, 60);
        sensor_reads = true; input_acceleration(2.0f, 0, 0, 120);
        // The valley before an I2C failure cannot join two later high samples.
        assert(s_pattern == pattern && !s_shake.blocked);
        input_acceleration(0, 0, 1, 60);
        input_acceleration(-2.0f, 0, 0, 60);
        assert(s_pattern == (pattern + 1) % PIXEL_FIELD_PATTERNS);
        capture("shake"); uint32_t fired = s_shake.fired_at; pattern = s_pattern;
        input_acceleration(2.0f, 0, 0, 1800);
        assert(s_pattern == pattern && s_shake.fired_at == fired && s_shake.blocked);
        input_acceleration(0, 0, 1, 400); shake_pulses();
        assert(s_pattern == (pattern + 1) % PIXEL_FIELD_PATTERNS && s_shake.fired_at != fired);
        // Read failure cannot prevent touch; it must not count stale samples as a shake.
        sensor_reads = false; before_reads = acceleration_reads; pattern = s_pattern;
        input_acceleration(2.0f, 0, 0, 1000);
        assert(acceleration_reads > before_reads && s_pattern == pattern);
        tap(281, 397); settle();
        initial = raster_hash(s_field); brush(153, 219, 305, 257);
        assert(raster_hash(s_field) != initial); capture("read-failure");
        unsigned backs = back_actions; tap(133, 64); assert(back_actions == backs + 1);
        unsigned session_palette = s_palette, session_pattern = s_pattern;
        close_page(); assert(timer_count() == baseline_timers);
        sensor_present = false; sensor_reads = true; accel_x = accel_y = 0; accel_z = 1;
        new_page(); assert(s_field && !s_has_imu);
        assert(s_palette == session_palette && s_pattern == session_pattern);
        initial = raster_hash(s_field); brush(145, 240, 315, 240);
        assert(raster_hash(s_field) != initial); capture("noimu");
        close_page(); assert(timer_count() == baseline_timers);
    }
    for(unsigned repeat = 0; repeat < 20; ++repeat) {
        sensor_present = sensor_reads = true; accel_x = accel_y = 0; accel_z = 1;
        new_page(); brush(180, 210, 290, 260);
        if(repeat % 2) app_pixels.visibility(false);
        if(repeat % 3 == 0) delete_page_without_exit(); else close_page();
        assert(!s_field && !s_body && !s_pm && !s_pm_held && !live && timer_count() == baseline_timers);
    }
    allocations = 0; fail_at = 1; new_page();
    assert(!s_field && live == 0); capture("allocation-failure");
    unsigned backs = back_actions; tap(133, 64); assert(back_actions == backs + 1);
    close_page(); fail_at = 0;
    puts("Pixels EN/ZH: native touch/buttons/shake, settled no-redraw, visibility/time reset, no/failing IMU, shared back, glyph/circle bounds and repeated/allocation-failure cleanup passed.");
}
int main(int argc, char **argv) {
    folder = argc > 1 ? argv[1] : NULL;
    lv_init(); i18n_init();
    display = lv_display_create(W, W); lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, buffer, NULL, sizeof buffer, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
    input = lv_indev_create(); lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(input, read_pointer); lv_timer_set_period(lv_indev_get_read_timer(input), 20);
    font_history_coverage(); shake_detector_boundaries();
    field_input_boundaries(); interaction_and_lifecycle();
    lv_deinit(); return 0;
}
