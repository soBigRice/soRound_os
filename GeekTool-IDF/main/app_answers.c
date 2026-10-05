#include "app.h"
#include "answer_messages.h"
#include "lvgl_compat.h"
#include "settings.h"
#include "esp_random.h"
#include <stdio.h>

LV_FONT_DECLARE(font_answer_32);
LV_FONT_DECLARE(font_location_24);

#define TURN_MS 720
#define HALF_TURN_MS (TURN_MS / 2)
#define PAPER 0xf5f5f2
#define MUTED 0xa0a0a6

static lv_obj_t *g_cover, *g_reading, *g_answer, *g_page, *g_hint, *g_action, *g_action_label;
static bool s_visible, s_turning, s_first, s_changed;
static uint32_t s_at, s_elapsed;
static unsigned s_pages, s_pending;
static int s_last;

static const char *text(const char *en, const char *zh) { return settings_lang() ? zh : en; }

static lv_obj_t *label(lv_obj_t *parent, const char *value, const lv_font_t *font,
                       int width, uint32_t color) {
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(obj, width);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(obj, value);
    ui_obj_set_event_bubble(obj, true);
    return obj;
}

static lv_obj_t *block(lv_obj_t *parent, int x, int y, int width, int height, uint32_t color) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y); lv_obj_set_size(obj, width, height);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    ui_obj_set_scrollable(obj, false); ui_obj_set_clickable(obj, false);
    ui_obj_set_event_bubble(obj, true); ui_obj_set_gesture_bubble(obj, true);
    return obj;
}

static void apply_answer(void) {
    lv_label_set_text(g_answer, settings_lang() ? ANSWERS[s_pending].zh : ANSWERS[s_pending].en);
    lv_obj_update_layout(g_answer);
    lv_obj_set_pos(g_answer, 7, 126 - lv_obj_get_height(g_answer) / 2);
    char page[24]; snprintf(page, sizeof page, "PAGE %02u", s_pages + 1);
    lv_label_set_text(g_page, page);
    s_changed = true;
}

static void start_turn(lv_event_t *event) {
    (void)event;
    if (!g_cover || !s_visible || s_turning) return;
    // Sample once per accepted tap; skip the previous index without an unbounded reroll loop.
    unsigned choices = (unsigned)ANSWER_COUNT - (s_last >= 0 ? 1u : 0u);
    s_pending = esp_random() % choices;
    if (s_last >= 0 && s_pending >= (unsigned)s_last) ++s_pending;
    s_first = s_last < 0; s_turning = true; s_changed = false;
    s_elapsed = 0; s_at = lv_tick_get();
    if (s_pages == 9999) s_pages = 0;
    lv_obj_add_state(g_action, LV_STATE_DISABLED);
    lv_obj_add_state(g_action_label, LV_STATE_DISABLED);
    lv_label_set_text(g_action_label, text("TURNING…", "翻页中"));
    lv_label_set_text(g_hint, text("TURNING THE PAGE", "正在翻页…"));
}

static void answers_tick(void) {
    if (!g_cover || !s_visible || !s_turning) return;
    uint32_t now = lv_tick_get(), delta = now - s_at; s_at = now;
    s_elapsed += delta > TURN_MS - s_elapsed ? TURN_MS - s_elapsed : delta;
    if (s_elapsed < HALF_TURN_MS) {
        if (s_first) {
            lv_obj_set_style_transform_scale_x(g_cover, 256 - 220 * s_elapsed / HALF_TURN_MS, 0);
        } else {
            lv_obj_set_style_opa(g_reading, 255 - 255 * s_elapsed / HALF_TURN_MS, 0);
            lv_obj_set_style_translate_y(g_reading, -(int)(8 * s_elapsed / HALF_TURN_MS), 0);
        }
    } else {
        if (!s_changed) apply_answer();
        ui_obj_set_hidden(g_cover, true); ui_obj_set_hidden(g_reading, false);
        unsigned phase = s_elapsed - HALF_TURN_MS;
        lv_obj_set_style_opa(g_reading, 255 * phase / HALF_TURN_MS, 0);
        lv_obj_set_style_translate_y(g_reading, 8 - 8 * phase / HALF_TURN_MS, 0);
    }
    if (s_elapsed < TURN_MS) return;
    s_turning = false; s_last = (int)s_pending; ++s_pages;
    lv_obj_remove_state(g_action, LV_STATE_DISABLED);
    lv_obj_remove_state(g_action_label, LV_STATE_DISABLED);
    lv_label_set_text(g_action_label, text("TURN A PAGE", "再翻一页"));
    lv_label_set_text(g_hint, text("A NEW PERSPECTIVE", "换个角度，再想一想"));
}

static void answers_visibility(bool visible) {
    s_visible = visible;
    // The launcher stops hidden ticks. Reset the clock so resuming cannot jump over a page turn.
    s_at = lv_tick_get();
}

static void answers_enter(lv_obj_t *parent) {
    s_last = -1; s_pages = 0; s_elapsed = 0;
    s_turning = s_changed = false; s_visible = true; s_at = lv_tick_get();
    lv_obj_t *root = block(parent, 0, 0, 466, 466, COL_BG);
    g_cover = block(root, 144, 114, 178, 216, 0x16161a);
    ui_obj_set_clickable(g_cover, true);
    lv_obj_set_style_radius(g_cover, 12, 0);
    lv_obj_set_style_border_width(g_cover, 2, 0);
    lv_obj_set_style_border_color(g_cover, lv_color_hex(PAPER), 0);
    lv_obj_set_style_transform_pivot_x(g_cover, 89, 0);
    lv_obj_set_style_transform_pivot_y(g_cover, 108, 0);
    lv_obj_add_event_cb(g_cover, start_turn, LV_EVENT_CLICKED, NULL);
    block(g_cover, 16, 14, 2, 184, 0x4c4c52);
    block(g_cover, 136, 0, 12, 32, COL_RED);
    lv_obj_t *title = label(g_cover, text("THE\nANSWER\nBOOK", "答案\n之书"), &font_answer_32, 148, PAPER);
    lv_obj_set_pos(title, 24, settings_lang() ? 60 : 39);
    block(g_cover, 78, 179, 26, 2, COL_RED);

    g_reading = block(root, 82, 120, 302, 216, COL_BG);
    ui_obj_set_clickable(g_reading, true);
    lv_obj_add_event_cb(g_reading, start_turn, LV_EVENT_CLICKED, NULL);
    g_page = label(g_reading, "", &font_location_24, 302, MUTED);
    lv_obj_set_style_text_letter_space(g_page, 2, 0);
    lv_obj_set_pos(g_page, 0, 0);
    block(g_reading, 139, 49, 24, 2, COL_RED);
    g_answer = label(g_reading, "", &font_answer_32, 288, PAPER);
    lv_obj_set_style_text_line_space(g_answer, 7, 0);
    ui_obj_set_hidden(g_reading, true);

    g_hint = label(root, text("THINK OF A QUESTION", "在心里想一个问题"), &font_location_24, 302, MUTED);
    lv_obj_set_pos(g_hint, 82, 344);
    g_action = block(root, 129, 382, 208, 46, PAPER);
    ui_obj_set_clickable(g_action, true);
    lv_obj_set_style_radius(g_action, 23, 0);
    lv_obj_set_style_bg_color(g_action, lv_color_hex(0x26262a), LV_STATE_DISABLED);
    lv_obj_add_event_cb(g_action, start_turn, LV_EVENT_CLICKED, NULL);
    g_action_label = label(g_action, text("OPEN A PAGE", "翻开一页"), &font_location_24, 200, COL_BG);
    lv_obj_set_style_text_color(g_action_label, lv_color_hex(MUTED), LV_STATE_DISABLED);
    lv_obj_center(g_action_label);
}

static void answers_exit(void) {
    s_visible = s_turning = false;
    g_cover = g_reading = g_answer = g_page = g_hint = g_action = g_action_label = NULL;
}

const app_t app_answers = {
    .name = "Answers", .color = COL_TXT, .enter = answers_enter,
    .tick = answers_tick, .exit = answers_exit, .tick_period_ms = 20,
    .visibility = answers_visibility,
};
