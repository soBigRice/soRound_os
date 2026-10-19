#include "app.h"
#include "audio_out.h"
#include "buttons.h"
#include "esp_timer.h"
#include "focus_state.h"
#include "font_companion.h"
#include "garden_state.h"
#include "lvgl_compat.h"
#include "settings.h"
#include "ui_update.h"
#include "focus_artwork.inc"
#include <stdio.h>

static focus_state_t s_focus;
static focus_preferences_t s_preferences;
static lv_obj_t *g_record, *g_timer, *g_scene, *g_hint, *g_toggle, *g_sound, *g_dots[3];
static bool s_visible, s_preferences_ready, s_preferences_dirty, s_audio_failed, s_completion_saved;
static uint32_t s_visual_at, s_saved_at, s_preferences_saved_at;
static int32_t s_rotation;
static const char *focus_words(const char *en, const char *zh) { return settings_lang() ? zh : en; }
static const char *focus_scene_name(void) {
    if (s_preferences.scene == AUDIO_SCENE_OCEAN) return focus_words("OCEAN WAVES", "海浪");
    if (s_preferences.scene == AUDIO_SCENE_FIRE) return focus_words("CAMPFIRE", "篝火");
    return focus_words("FOREST RAIN", "森林雨声");
}
static lv_obj_t *focus_label(lv_obj_t *p, const char *text, int y, int width, const lv_font_t *font, uint32_t color) {
    lv_obj_t *o = lv_label_create(p); lv_obj_set_width(o, width); lv_label_set_text(o, text);
    lv_obj_set_style_text_font(o, font, 0); lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_CENTER, 0); lv_obj_align(o, LV_ALIGN_TOP_MID, 0, y); return o;
}
static lv_obj_t *focus_button(lv_obj_t *p, int x, int y, int size, const char *symbol, lv_event_cb_t cb) {
    lv_obj_t *o = lv_button_create(p); lv_obj_set_size(o, size, size); lv_obj_set_pos(o, x, y);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_color(o, lv_color_hex(0x101418), 0);
    lv_obj_set_style_border_width(o, 1, 0); lv_obj_set_style_border_color(o, lv_color_hex(0x343a40), 0);
    lv_obj_set_style_shadow_width(o, 0, 0); lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_t *label = lv_label_create(o); lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_label_set_text(label, symbol); lv_obj_center(label); lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, NULL); return label;
}
static void focus_save_preferences(void) {
    if (s_preferences_dirty && focus_preferences_save(&s_preferences)) s_preferences_dirty = false;
    s_preferences_saved_at = lv_tick_get();
}
static void focus_sync_audio(void) {
    if (s_focus.phase != FOCUS_RUNNING || s_preferences.muted || settings_silent()) {
        audio_out_scene_stop(); s_audio_failed = false; return;
    }
    s_audio_failed = !audio_out_scene_start((audio_scene_t)s_preferences.scene);
}
static void focus_refresh(void) {
    if (!g_timer) return;
    unsigned seconds = (unsigned)((focus_state_remaining_us(&s_focus, esp_timer_get_time()) + 999999) / 1000000);
    char value[12]; snprintf(value, sizeof value, "%02u:%02u", seconds / 60, seconds % 60); ui_text(g_timer, value);
    ui_text(g_scene, focus_scene_name()); ui_text(g_toggle, s_focus.phase == FOCUS_RUNNING ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    ui_text(g_sound, s_preferences.muted || settings_silent() ? LV_SYMBOL_MUTE : LV_SYMBOL_VOLUME_MAX);
    if (s_focus.phase == FOCUS_DONE) ui_text(g_hint, s_completion_saved ? focus_words("DONE · A NEW LEAF IS GROWING", "完成，小树长出了新叶") : focus_words("DONE · PROGRESS NOT SAVED", "完成，进度尚未保存"));
    else if (s_preferences_dirty) ui_text(g_hint, focus_words("SETTINGS NOT SAVED", "设置尚未保存"));
    else if (!s_preferences_ready) ui_text(g_hint, focus_words("SETTINGS LOAD FAILED", "设置读取失败"));
    else if (s_audio_failed) ui_text(g_hint, focus_words("SOUND UNAVAILABLE · TIMER ON", "声音不可用，计时继续"));
    else if (settings_silent()) ui_text(g_hint, focus_words("SYSTEM MUTE · TIMER READY", "系统静音，计时可用"));
    else if (s_focus.phase == FOCUS_PAUSED) ui_text(g_hint, focus_words("PAUSED · TAP PLAY TO RESUME", "已暂停，点击继续"));
    else ui_text(g_hint, focus_words("ONE MOMENT, ONE THING", "一段时间，只做一件事"));
    for (int i = 0; i < 3; ++i) ui_bg_color(g_dots[i], s_preferences.scene == i + 1 ? 0xe95236 : 0x42484c);
}
static void focus_finish_if_due(void) {
    focus_state_update(&s_focus, esp_timer_get_time());
    if (!focus_state_take_completion(&s_focus)) return;
    if (!settings_silent()) (void)audio_out_alarm_once();
    audio_out_scene_stop();
    garden_state_t *garden = garden_shared_state();
    if (garden_shared_ready() && garden_state_complete_focus(garden, s_focus.session_id, 25, garden_day_key())) {
        garden_shared_mark_dirty(); s_completion_saved = garden_shared_save();
    }
    // 完成事件只消费一次；重绘、解锁与重入不会再次增长。
    // Consume completion once; redraw, unlock and re-entry cannot add growth again.
}
static void focus_toggle(lv_event_t *e) {
    (void)e; if (!s_visible) return;
    focus_finish_if_due();
    if (s_focus.phase == FOCUS_DONE) focus_state_reset(&s_focus, garden_state_next_focus_id(garden_shared_state()));
    focus_state_toggle(&s_focus, esp_timer_get_time()); focus_sync_audio(); focus_refresh();
}
static void focus_reset(lv_event_t *e) {
    (void)e; if (!s_visible) return;
    // 临界到期先结算，避免重置按钮吞掉一个已经完成的专注。
    // Settle a reached deadline before resetting so a completed session is not lost.
    focus_finish_if_due(); audio_out_scene_stop();
    focus_state_reset(&s_focus, garden_state_next_focus_id(garden_shared_state())); s_completion_saved = false;
    s_rotation = 0; lv_image_set_rotation(g_record, 0); focus_refresh();
}
static void focus_sound(lv_event_t *e) {
    (void)e; if (!s_visible) return;
    if (!s_preferences_ready) s_preferences_ready = focus_preferences_load(&s_preferences);
    if (!s_preferences_ready) { focus_refresh(); return; }
    s_preferences.muted = !s_preferences.muted; s_preferences_dirty = true;
    focus_save_preferences(); focus_sync_audio(); focus_refresh();
}
static void focus_next_scene(lv_event_t *e) {
    (void)e; if (!s_visible) return;
    if (!s_preferences_ready) s_preferences_ready = focus_preferences_load(&s_preferences);
    if (!s_preferences_ready) { focus_refresh(); return; }
    s_preferences.scene = (uint8_t)(s_preferences.scene % 3 + 1); s_preferences_dirty = true;
    focus_save_preferences(); focus_sync_audio(); focus_refresh();
}
static void focus_needle(lv_event_t *e) {
    lv_layer_t *layer = lv_event_get_layer(e); lv_obj_t *o = lv_event_get_target_obj(e);
    lv_area_t a; lv_obj_get_coords(o, &a);
    lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d); d.base.obj = o;
    d.color = lv_color_hex(0x34383c); d.width = 9; d.round_start = d.round_end = true;
    d.p1 = (lv_point_precise_t){a.x1 + 40, a.y1 + 12}; d.p2 = (lv_point_precise_t){a.x1 + 17, a.y1 + 109}; lv_draw_line(layer, &d);
    d.color = lv_color_hex(0x7d8387); d.width = 3;
    d.p1.x -= 2; d.p2.x -= 2; lv_draw_line(layer, &d);
    d.color = lv_color_hex(0xd64d3e); d.width = 4;
    d.p1 = (lv_point_precise_t){a.x1 + 17, a.y1 + 107}; d.p2 = (lv_point_precise_t){a.x1 + 11, a.y1 + 122}; lv_draw_line(layer, &d);
}
static void focus_tick(void) {
    if (!g_timer) return;
    focus_finish_if_due();
    uint32_t now = lv_tick_get();
    if (garden_shared_dirty() && now - s_saved_at >= 10000) {
        s_completion_saved = garden_shared_save(); s_saved_at = now;
    }
    if (s_preferences_dirty && now - s_preferences_saved_at >= 10000) focus_save_preferences();
    if (!s_visible) return;
    if (buttons_control_pressed()) focus_toggle(NULL);
    if (s_focus.phase == FOCUS_RUNNING) {
        // 只在可见时旋转；业务计时完全由单调时钟计算。
        // Rotate only while visible; business timing uses the monotonic clock.
        uint32_t delta = now - s_visual_at; if (delta > 200) delta = 200;
        s_rotation = (s_rotation + (int32_t)(delta * 3 / 10)) % 3600; lv_image_set_rotation(g_record, s_rotation);
    }
    s_visual_at = now; focus_refresh();
}
static void focus_visibility(bool visible) {
    s_visible = visible; buttons_reset_control(); s_visual_at = lv_tick_get();
    if (visible) { focus_finish_if_due(); focus_refresh(); }
    else { focus_save_preferences(); (void)garden_shared_save(); }
}
static void focus_enter(lv_obj_t *parent) {
    garden_state_t *garden = garden_shared_state();
    focus_state_init(&s_focus, garden_state_next_focus_id(garden));
    if (!s_preferences_dirty) {
        s_preferences = (focus_preferences_t){.scene = AUDIO_SCENE_RAIN};
        s_preferences_ready = focus_preferences_load(&s_preferences);
    }
    s_visible = true; s_audio_failed = s_completion_saved = false; s_rotation = 0;
    s_visual_at = s_saved_at = s_preferences_saved_at = lv_tick_get(); buttons_reset_control();
    launcher_set_title(focus_words("FOCUS", "专注"));
    lv_obj_t *subtitle = focus_label(parent, "FOCUS DECK", 84, 240, &font_companion_19, 0x7c8084);
    lv_obj_set_style_text_letter_space(subtitle, 3, 0);
    // A8 外底让旋转只绘制唱片，黑色方角不会盖住邻近文案。
    // The A8 backdrop draws only the record, keeping black square corners off nearby UI.
    g_record = lv_image_create(parent); lv_image_set_src(g_record, &focus_image);
    lv_obj_set_pos(g_record, (466 - FOCUS_ART_WIDTH) / 2, 106);
    lv_image_set_pivot(g_record, FOCUS_ART_WIDTH / 2, FOCUS_ART_HEIGHT / 2);
    lv_obj_t *needle = lv_obj_create(parent); lv_obj_remove_style_all(needle);
    lv_obj_set_size(needle, 58, 132); lv_obj_set_pos(needle, 315, 111); ui_obj_set_clickable(needle, false);
    lv_obj_add_event_cb(needle, focus_needle, LV_EVENT_DRAW_MAIN, NULL);
    g_timer = focus_label(parent, "25:00", 213, 180, &lv_font_montserrat_40, 0xffecdb);
    g_scene = focus_label(parent, "", 263, 180, &font_companion_19, 0xffd7c4);
    g_hint = focus_label(parent, "", 365, 320, &font_companion_19, 0xb7b9ba);
    g_sound = focus_button(parent, 119, 391, 44, LV_SYMBOL_VOLUME_MAX, focus_sound);
    g_toggle = focus_button(parent, 204, 390, 56, LV_SYMBOL_PLAY, focus_toggle);
    (void)focus_button(parent, 303, 391, 44, LV_SYMBOL_REFRESH, focus_reset);
    ui_obj_set_clickable(g_scene, true); lv_obj_add_event_cb(g_scene, focus_next_scene, LV_EVENT_CLICKED, NULL);
    for (int i = 0; i < 3; ++i) {
        g_dots[i] = lv_obj_create(parent); lv_obj_remove_style_all(g_dots[i]); lv_obj_set_size(g_dots[i], 8, 8);
        lv_obj_set_pos(g_dots[i], 210 + i * 19, 448); lv_obj_set_style_radius(g_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(g_dots[i], LV_OPA_COVER, 0);
    }
    // 唱片可点按换音景，三个下方按钮专职静音/计时/重置。
    // Tap the record to change scene; lower buttons handle sound, timer and reset.
    ui_obj_set_clickable(g_record, true); lv_obj_add_event_cb(g_record, focus_next_scene, LV_EVENT_CLICKED, NULL);
    focus_refresh();
}
static void focus_exit(void) {
    focus_finish_if_due(); audio_out_scene_stop(); focus_save_preferences(); (void)garden_shared_save();
    s_visible = false; focus_state_init(&s_focus, 0); buttons_reset_control();
    g_record = g_timer = g_scene = g_hint = g_toggle = g_sound = NULL;
    for (int i = 0; i < 3; ++i) g_dots[i] = NULL;
}
const app_t app_focus = {.name = "Focus", .color = COL_TXT, .enter = focus_enter, .tick = focus_tick,
    .exit = focus_exit, .tick_period_ms = 100, .visibility = focus_visibility, .tick_in_background = true};
