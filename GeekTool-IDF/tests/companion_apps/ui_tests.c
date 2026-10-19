// Real LVGL renderer and both app implementations; substitute only hardware/NVS.
// 使用真实 LVGL 与 App，只替换硬件输入、音频总线和 NVS。
#include "app.h"
#include "font_companion.h"
#include "lvgl_compat.h"
#include "settings.h"
#include "nvs.h"
#include "audio_out.h"
#include "src/misc/lv_text_private.h"
#include "../host/tools_render.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint8_t language;
static bool silent, audio_available = true, audio_active, control;
static unsigned alarm_requests, scene_stops;
static audio_scene_t last_scene;
static lv_obj_t *heading;
int64_t host_time_us;
uint8_t settings_lang(void) { return language; }
uint8_t settings_silent(void) { return silent; }
void buttons_reset_control(void) { control = false; }
bool buttons_control_pressed(void) { bool pressed = control; control = false; return pressed; }
void launcher_set_title(const char *text) { lv_label_set_text(heading, text); }
bool audio_out_scene_start(audio_scene_t scene) { last_scene = scene; audio_active = audio_available && !silent; return audio_active; }
void audio_out_scene_stop(void) { audio_active = false; ++scene_stops; }
bool audio_out_scene_active(void) { return audio_active; }
bool audio_out_alarm_once(void) { if (silent) return false; ++alarm_requests; return true; }
static uint8_t saved_garden[32], pending_garden[32];
static uint32_t saved_pref = 1, pending_pref;
static bool garden_exists;
static int commit_error;
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *h) {
    (void)mode; assert(!strcmp(name, "garden") || !strcmp(name, "focus")); *h = !strcmp(name, "garden") ? 1 : 2; return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *value, size_t *length) {
    assert(h == 1 && !strcmp(key, "state_v1") && *length == sizeof saved_garden);
    if (!garden_exists) return ESP_ERR_NVS_NOT_FOUND;
    memcpy(value, saved_garden, sizeof saved_garden); return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *value, size_t length) {
    assert(h == 1 && !strcmp(key, "state_v1") && length == sizeof saved_garden);
    memcpy(pending_garden, value, length); return ESP_OK;
}
esp_err_t nvs_get_u32(nvs_handle_t h, const char *key, uint32_t *value) { assert(h == 2 && !strcmp(key, "prefs_v1")); *value = saved_pref; return ESP_OK; }
esp_err_t nvs_set_u32(nvs_handle_t h, const char *key, uint32_t value) { assert(h == 2 && !strcmp(key, "prefs_v1")); pending_pref = value; return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t h) {
    if (commit_error) return commit_error;
    if (h == 1) { memcpy(saved_garden, pending_garden, sizeof saved_garden); garden_exists = true; }
    else saved_pref = pending_pref;
    return ESP_OK;
}
void nvs_close(nvs_handle_t h) { assert(h == 1 || h == 2); }

#include "../../main/app_garden.c"
#define s_visible focus_visible_for_test
#define s_saved_at focus_saved_at_for_test
#include "../../main/app_focus.c"
#undef s_saved_at
#undef s_visible

_Alignas(LV_DRAW_BUF_ALIGN) static uint16_t draw_buffer[466 * 40];
static uint16_t pixels[466 * 466];
static lv_display_t *display;
static unsigned flushed;
static void flush(lv_display_t *d, const lv_area_t *area, uint8_t *map) {
    int width = lv_area_get_width(area); flushed += (unsigned)(width * lv_area_get_height(area));
    for (int y = area->y1; y <= area->y2; ++y) { memcpy(pixels + y * 466 + area->x1, map, (size_t)width * 2); map += width * 2; }
    lv_display_flush_ready(d);
}
static void check_labels(lv_obj_t *o) {
    if (ui_obj_is_hidden(o)) return;
    if (lv_obj_check_type(o, &lv_label_class)) {
        const char *text = lv_label_get_text(o); uint32_t at = 0;
        while (text[at]) {
            uint32_t cp = lv_text_encoded_next(text, &at); if (cp == '\n') continue;
            lv_font_glyph_dsc_t glyph;
            if (!lv_font_get_glyph_dsc(lv_obj_get_style_text_font(o, 0), &glyph, cp, 0) || glyph.is_placeholder) {
                fprintf(stderr, "missing glyph U+%04x: %s\n", (unsigned)cp, text); assert(false);
            }
        }
        lv_area_t a; lv_obj_get_coords(o, &a);
        for (unsigned i = 0; i < 4; ++i) {
            int x = i & 1 ? a.x2 : a.x1, y = i & 2 ? a.y2 : a.y1;
            if (hypot(x - 232.5, y - 232.5) > 233) {
                fprintf(stderr, "label outside circle: %s (%d,%d)-(%d,%d)\n", text, a.x1, a.y1, a.x2, a.y2); assert(false);
            }
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); ++i) check_labels(lv_obj_get_child(o, i));
}
static void capture(const char *dir, const char *name) {
    lv_obj_update_layout(lv_screen_active()); lv_obj_update_layout(lv_layer_top());
    if (g_summary && g_hint && g_water) {
        lv_area_t summary, hint, water, light;
        lv_obj_get_coords(g_summary, &summary); lv_obj_get_coords(g_hint, &hint);
        lv_obj_get_coords(g_water, &water); lv_obj_get_coords(lv_obj_get_parent(g_light), &light);
        // 字体行高也占据布局空间，所有实际文案都必须与下一行/按钮分离。
        // Font line height occupies layout space; every real label must clear the next row and controls.
        assert(summary.y2 < hint.y1);
        assert(hint.y2 < water.y1 && hint.y2 < light.y1);
    }
    check_labels(lv_screen_active()); check_labels(lv_layer_top()); lv_refr_now(display);
    if (!dir) return;
    char path[1024]; snprintf(path, sizeof path, "%s/%s-%s.ppm", dir, name, language ? "zh" : "en");
    FILE *f = fopen(path, "wb"); assert(f); fprintf(f, "P6\n466 466\n255\n");
    for (unsigned i = 0; i < 466 * 466; ++i) {
        uint16_t p = pixels[i]; uint8_t rgb[] = {((p >> 11) & 31) * 255 / 31, ((p >> 5) & 63) * 255 / 63, (p & 31) * 255 / 31}; fwrite(rgb, 1, 3, f);
    }
    assert(fclose(f) == 0);
}
static lv_obj_t *new_page(void) {
    lv_obj_t *p = lv_obj_create(lv_screen_active()); lv_obj_remove_style_all(p); lv_obj_set_size(p, 466, 466); ui_obj_set_scrollable(p, false); return p;
}
static void step(uint32_t milliseconds) { lv_tick_inc(milliseconds); host_time_us += (int64_t)milliseconds * 1000; }
static uint32_t side_button_pixels(void) {
    uint32_t hash = 2166136261u;
    for (int y = 392; y < 434; ++y) for (int x = 120; x < 162; ++x) hash = (hash ^ pixels[y * 466 + x]) * 16777619u;
    return hash;
}
int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : NULL;
    lv_init(); i18n_init(); display = lv_display_create(466, 466); lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, draw_buffer, NULL, sizeof draw_buffer, LV_DISPLAY_RENDER_MODE_PARTIAL); lv_display_set_flush_cb(display, flush);
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0), 0); tools_test_battery();
    heading = lv_label_create(lv_layer_top()); lv_obj_set_style_text_font(heading, &font_companion_24, 0);
    lv_obj_set_style_text_color(heading, lv_color_hex(COL_TXT), 0); lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 51);
    for (language = 0; language < 2; ++language) {
        lv_obj_t *p = new_page(); garden_enter(p); capture(dir, "garden-ready");
        uint32_t count = s_state->waterings; garden_water(NULL); assert(s_state->waterings == count + 1);
        step(250); garden_tick(); capture(dir, "garden-water");
        garden_water(NULL); assert(s_state->waterings == count + 1);
        garden_light(NULL); garden_light(NULL); assert(s_state->light == GARDEN_LIGHT_NIGHT); capture(dir, "garden-night");
        assert(pixels[107 * 466 + 70] == 0); // recolor must not tint the transparent outside backdrop
        garden_visibility(false); int y = lv_obj_get_y(g_drops[0]); step(3000); garden_tick(); assert(lv_obj_get_y(g_drops[0]) == y);
        garden_visibility(true); garden_tick(); capture(NULL, "garden-resume");
        commit_error = ESP_FAIL; garden_light(NULL); assert(garden_shared_dirty()); capture(dir, "garden-unsaved");
        commit_error = 0; garden_exit(); assert(!garden_shared_dirty()); lv_obj_delete(p);

        saved_pref = 1; p = new_page(); focus_enter(p); capture(dir, "focus-ready"); uint32_t side_pixels = side_button_pixels();
        focus_toggle(NULL); assert(s_focus.phase == FOCUS_RUNNING && audio_active);
        step(1000); focus_tick(); capture(dir, "focus-running"); int32_t rotation = s_rotation;
        assert(side_button_pixels() == side_pixels); // a rotated image cannot erase a sibling control
        focus_visibility(false); step(10000); focus_tick(); assert(s_rotation == rotation && s_focus.phase == FOCUS_RUNNING);
        focus_visibility(true); focus_toggle(NULL); assert(s_focus.phase == FOCUS_PAUSED && !audio_active);
        int64_t remaining = focus_state_remaining_us(&s_focus, host_time_us); step(10000); focus_tick();
        assert(focus_state_remaining_us(&s_focus, host_time_us) == remaining); capture(dir, "focus-paused");
        focus_next_scene(NULL); assert(last_scene == AUDIO_SCENE_RAIN && s_preferences.scene == AUDIO_SCENE_OCEAN);
        focus_toggle(NULL); assert(audio_active && last_scene == AUDIO_SCENE_OCEAN);
        focus_next_scene(NULL); assert(last_scene == AUDIO_SCENE_FIRE); capture(dir, "focus-fire");
        uint32_t sessions = garden_shared_state()->focus_sessions; unsigned alerts = alarm_requests;
        commit_error = ESP_FAIL; focus_sound(NULL); assert(s_preferences_dirty);
        focus_visibility(false); host_time_us += FOCUS_DURATION_US; step(100); focus_tick();
        assert(s_focus.phase == FOCUS_DONE && !audio_active && garden_shared_state()->focus_sessions == sessions + 1 && alarm_requests == alerts + 1);
        assert(garden_shared_dirty() && !s_completion_saved); capture(dir, "focus-unsaved");
        commit_error = 0; step(10000); focus_tick();
        assert(!garden_shared_dirty() && !s_preferences_dirty && s_completion_saved);
        focus_tick(); focus_visibility(true); focus_tick(); assert(garden_shared_state()->focus_sessions == sessions + 1 && alarm_requests == alerts + 1); capture(dir, "focus-done");
        focus_reset(NULL); assert(s_focus.phase == FOCUS_READY && focus_state_remaining_us(&s_focus, host_time_us) == FOCUS_DURATION_US);
        focus_sound(NULL); audio_available = false; focus_toggle(NULL); assert(s_audio_failed && s_focus.phase == FOCUS_RUNNING); capture(dir, "focus-no-audio");
        audio_available = true; unsigned stops = scene_stops; focus_exit(); assert(!audio_active && scene_stops > stops && s_focus.session_id == 0); lv_obj_delete(p);
        p = new_page(); garden_enter(p); capture(dir, "garden-grown"); garden_exit(); lv_obj_delete(p);
    }
    printf("garden/focus native LVGL: bilingual glyphs, circle bounds, watering/night, hidden animation, timer boundaries, one completion/growth, audio failure and exit passed (%u flushed pixels)\n", flushed);
    lv_deinit(); return 0;
}
