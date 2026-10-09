// Pixel Field: native vector squares, bounded spring motion and local IMU input.
#include "app.h"
#include "pixel_field.h"
#include "tools_ui.h"
#include "ui_update.h"
#include "imu.h"
#include "esp_heap_caps.h"
#include "esp_pm.h"
#include <math.h>

static pixel_field_t *s_field;
static pixel_shake_t s_shake;
static lv_obj_t *s_body, *s_buttons[2], *s_hint;
static bool s_visible, s_has_imu, s_pm_held;
static esp_pm_lock_handle_t s_pm;
static uint8_t s_palette, s_pattern; // Session choices only; no new persistent settings.
static uint32_t s_last_frame_ms, s_last_imu_ms;
LV_FONT_DECLARE(font_weather_16);
LV_FONT_DECLARE(font_cn16);
static lv_font_t s_text_font;
static const uint32_t PALETTES[PIXEL_FIELD_PALETTES][6] = {
    {0xee775c,0xf1bf66,0x66b7ad,0x81a4d8,0xc95d50,0x96d0c1},
    {0x77afd1,0xf0cf87,0x93cbb9,0xdf8c6d,0x517eaa,0xbedfdb},
    {0xa6c889,0xe6cb8c,0x6bbdaf,0xae9bca,0x688d72,0xc6dda8}
};

static void pm_hold(bool wanted) {
    if (!s_pm || wanted == s_pm_held) return;
    if (wanted) { if (esp_pm_lock_acquire(s_pm) == ESP_OK) s_pm_held = true; }
    else if (esp_pm_lock_release(s_pm) == ESP_OK) s_pm_held = false;
}

static void field_redraw(void) {
    if (!s_body) return;
    lv_area_t bounds; lv_obj_get_coords(s_body, &bounds);
    lv_area_t dirty = {bounds.x1 + 42, bounds.y1 + 116, bounds.x1 + 424, bounds.y1 + 335};
    lv_obj_invalidate_area(s_body, &dirty);
}

static void square(lv_layer_t *layer, int x, int y, int size, uint32_t color) {
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(color); d.bg_opa = LV_OPA_COVER;
    lv_area_t area = {x, y, x + size - 1, y + size - 1};
    lv_draw_rect(layer, &d, &area);
}

static void field_draw(lv_event_t *event) {
    if (!s_field) return;
    lv_area_t bounds; lv_obj_get_coords(s_body, &bounds);
    lv_layer_t *layer = lv_event_get_layer(event);
    for (unsigned i = 0; i < s_field->count; ++i) {
        const pixel_field_tile_t *tile = &s_field->tiles[i];
        int x = bounds.x1 + (int)lroundf(tile->x) - PIXEL_FIELD_TILE_SIZE / 2;
        int y = bounds.y1 + (int)lroundf(tile->y) - PIXEL_FIELD_TILE_SIZE / 2;
        uint32_t color = tile->color < 0 ? 0x111318 : PALETTES[s_palette][tile->color];
        square(layer, x, y, PIXEL_FIELD_TILE_SIZE, color);
    }
}

static void hint_update(void) {
    if (!s_hint) return;
    const char *text = !s_field ? tools_text("Not enough memory", "内存不足") :
        s_has_imu ? tools_text("DRAG TO PLAY / SHAKE TO REMIX", "拖动拨开 · 摇动重排") :
                    tools_text("TOUCH WORKS / SHAKE UNAVAILABLE", "触摸可玩 · 传感器不可用");
    ui_text(s_hint, text);
    tools_label_center(s_hint, 233, 350);
}

static void wake(void) {
    pm_hold(s_visible && s_field && s_field->active);
}

static void pixel_touch(lv_event_t *event) {
    if (!s_field || !s_visible) return;
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        pixel_field_touch(s_field, 0, 0, false);
        return;
    }
    lv_indev_t *input = lv_indev_active();
    if (!input) return;
    lv_point_t point; lv_indev_get_point(input, &point);
    lv_area_t bounds; lv_obj_get_coords(s_body, &bounds);
    pixel_field_touch(s_field, point.x - bounds.x1, point.y - bounds.y1, true);
    wake();
}

static void button_draw(lv_event_t *event) {
    unsigned action = (uintptr_t)lv_event_get_user_data(event);
    lv_area_t bounds; lv_obj_get_coords(lv_event_get_target_obj(event), &bounds);
    int cx = bounds.x1 + 40, y = bounds.y1 + 11;
    lv_layer_t *layer = lv_event_get_layer(event);
    if (action == 0) {
        for (unsigned i = 0; i < 4; ++i)
            square(layer, cx - 8 + (i % 2) * 9, y + (i / 2) * 9, 7, PALETTES[s_palette][i]);
    } else {
        for (unsigned i = 0; i < 4; ++i) {
            int dx = i % 2 ? 1 : -1, dy = i / 2 ? 1 : -1;
            int x = cx + dx * 7, yy = y + 8 + dy * 7;
            tools_line(layer, x, yy, x - dx * 4, yy, 1, TOOLS_WHITE);
            tools_line(layer, x, yy, x, yy - dy * 4, 1, TOOLS_WHITE);
        }
    }
}

static void pixel_action(lv_event_t *event) {
    if (!s_field || !s_visible) return;
    unsigned action = (uintptr_t)lv_event_get_user_data(event);
    if (action == 0) {
        s_palette = (s_palette + 1) % PIXEL_FIELD_PALETTES;
        lv_obj_invalidate(s_buttons[0]); field_redraw();
    } else { pixel_field_gather(s_field); wake(); }
}

static void pixel_tick(void) {
    if (!s_body || !s_field || !s_visible) return;
    uint32_t now = lv_tick_get();
    if ((uint32_t)(now - s_last_imu_ms) >= 50) {
        s_last_imu_ms = now;
        float x, y, z;
        bool valid = imu_read_accel(&x, &y, &z) && isfinite(x) && isfinite(y) && isfinite(z) &&
                     fabsf(x) <= 2.1f && fabsf(y) <= 2.1f && fabsf(z) <= 2.1f;
        if (valid) {
            if (pixel_shake_sample(&s_shake, x, y, z, now)) {
                s_pattern = (s_pattern + 1) % PIXEL_FIELD_PATTERNS;
                pixel_field_repattern(s_field, s_pattern, true); field_redraw(); wake();
            }
        } else { s_shake.peak = s_shake.fell = s_shake.quiet = false; }
        if (valid != s_has_imu) { s_has_imu = valid; hint_update(); }
    }
    uint32_t elapsed = now - s_last_frame_ms;
    if (elapsed < 33) return;
    s_last_frame_ms = now;
    if (pixel_field_step(s_field, elapsed / 1000.0f)) field_redraw();
    wake();
}

static void pixel_visibility(bool visible) {
    s_visible = visible;
    if (s_field) pixel_field_touch(s_field, 0, 0, false);
    // Neither stale input nor background elapsed time may turn into a new shake/impulse.
    pixel_shake_reset(&s_shake);
    s_last_frame_ms = s_last_imu_ms = lv_tick_get();
    wake();
}

static void pixel_exit(void) {
    s_visible = false; pm_hold(false);
    if (s_pm) { esp_pm_lock_delete(s_pm); s_pm = NULL; }
    if (s_field) { heap_caps_free(s_field); s_field = NULL; }
    s_body = s_hint = NULL; s_buttons[0] = s_buttons[1] = NULL;
    s_has_imu = false; pixel_shake_reset(&s_shake);
}

static void body_deleted(lv_event_t *event) {
    if (lv_event_get_target_obj(event) == s_body) pixel_exit();
}

static void pixel_enter(lv_obj_t *parent) {
    s_visible = true; s_last_frame_ms = s_last_imu_ms = lv_tick_get();
    pixel_shake_reset(&s_shake);
    s_text_font = font_weather_16; s_text_font.fallback = &font_cn16;
    s_body = tools_surface(parent, 0, 0, 466, 466);
    lv_obj_set_style_bg_color(s_body, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(s_body, LV_OPA_COVER, 0);
    // A rightward stroke belongs to the toy; the shared title button still returns home.
    ui_obj_set_gesture_bubble(s_body, false); ui_obj_set_event_bubble(s_body, false);
    ui_obj_set_clickable(s_body, true);
    lv_obj_add_event_cb(s_body, field_draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(s_body, body_deleted, LV_EVENT_DELETE, NULL);
    const lv_event_code_t events[] = {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST};
    for (unsigned i = 0; i < sizeof(events) / sizeof(events[0]); ++i)
        lv_obj_add_event_cb(s_body, pixel_touch, events[i], NULL);
    tools_label(s_body, "PIXEL FIELD", &s_text_font, 233, 101, TOOLS_GRAY, 2);
    s_hint = tools_label(s_body, "", &s_text_font, 233, 350, TOOLS_GRAY, 0);
    for (unsigned i = 0; i < 2; ++i) {
        lv_obj_t *button = s_buttons[i] = lv_button_create(s_body);
        lv_obj_remove_style_all(button); lv_obj_set_pos(button, 145 + (int)i * 96, 369); lv_obj_set_size(button, 80, 56);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x141518), 0); lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(button, 12, 0); lv_obj_set_style_border_width(button, 1, 0);
        lv_obj_set_style_border_color(button, lv_color_hex(0x323339), 0);
        ui_obj_set_scrollable(button, false); ui_obj_set_gesture_bubble(button, false); ui_obj_set_event_bubble(button, false);
        lv_obj_add_event_cb(button, button_draw, LV_EVENT_DRAW_MAIN, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(button, pixel_action, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        tools_label(button, i ? tools_text("Regroup", "重聚") : tools_text("Palette", "配色"),
                    &s_text_font, 40, 43, TOOLS_WHITE, 0);
    }
    s_field = heap_caps_malloc(sizeof(*s_field), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_field) {
        pixel_field_init(s_field, s_pattern);
        s_has_imu = imu_init();
        if (esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "pixels", &s_pm) != ESP_OK) s_pm = NULL;
    } else {
        s_has_imu = false;
        for (unsigned i = 0; i < 2; ++i) lv_obj_add_state(s_buttons[i], LV_STATE_DISABLED);
    }
    hint_update();
}

const app_t app_pixels = {"Pixels", COL_TXT, pixel_enter, pixel_tick, pixel_exit, NULL, 20, pixel_visibility};
