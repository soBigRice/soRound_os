#include "app.h"
#include "garden_state.h"
#include "font_companion.h"
#include "lvgl_compat.h"
#include "settings.h"
#include "ui_update.h"
#include "garden_artwork.inc"
#include <stdio.h>

static lv_obj_t *g_garden, *g_summary, *g_hint, *g_water, *g_light, *g_ripple, *g_drops[6], *g_leaves[6];
static garden_state_t *s_state;
static bool s_visible, s_watered, s_night;
static uint32_t s_hit_at, s_saved_at, s_last_day;
static const char *garden_words(const char *en, const char *zh) { return settings_lang() ? zh : en; }

static lv_obj_t *garden_label(lv_obj_t *p, const char *text, int y, int width, const lv_font_t *font, uint32_t color) {
    lv_obj_t *o = lv_label_create(p); lv_obj_set_width(o, width);
    lv_obj_set_style_text_font(o, font, 0); lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_CENTER, 0); lv_label_set_text(o, text);
    lv_obj_align(o, LV_ALIGN_TOP_MID, 0, y); return o;
}
static lv_obj_t *garden_button(lv_obj_t *p, int x, int y, int w, const char *text, lv_event_cb_t cb) {
    lv_obj_t *o = lv_button_create(p); lv_obj_set_pos(o, x, y); lv_obj_set_size(o, w, 40);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_color(o, lv_color_hex(0x111519), 0);
    lv_obj_set_style_border_width(o, 1, 0); lv_obj_set_style_border_color(o, lv_color_hex(0x353b40), 0);
    lv_obj_set_style_shadow_width(o, 0, 0); lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_t *label = lv_label_create(o); lv_obj_set_style_text_font(label, UI_FONT_SYM, 0);
    lv_label_set_text(label, text); lv_obj_center(label); lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, NULL); return o;
}
static void garden_refresh(void) {
    if (!g_garden) return;
    char value[96];
    if (!garden_shared_ready()) {
        ui_text(g_summary, garden_words("PROGRESS UNAVAILABLE", "进度读取失败"));
        ui_text(g_hint, garden_words("TAP WATER TO RETRY", "点浇水重试读取"));
    } else {
        uint32_t key = garden_day_key();
        if (key) snprintf(value, sizeof value, garden_words("TODAY  %lu MIN", "今日专注 %lu 分钟"), (unsigned long)s_state->today_minutes);
        else snprintf(value, sizeof value, garden_words("TOTAL  %lu MIN", "累计专注 %lu 分钟"), (unsigned long)s_state->focus_minutes);
        ui_text(g_summary, value);
        if (garden_shared_dirty()) ui_text(g_hint, garden_words("SAVE PENDING · TAP TO RETRY", "尚未保存 · 点击重试"));
        else if (s_watered && lv_tick_get() - s_hit_at < 1800) ui_text(g_hint, garden_words("A LITTLE WATER, A LITTLE JOY", "喝到水了，叶子动了"));
        else {
            snprintf(value, sizeof value, garden_words("%lu NEW LEAVES · KEEP GROWING", "%lu 片新叶 · 慢慢生长"), (unsigned long)garden_state_leaves(s_state));
            ui_text(g_hint, value);
        }
    }
    bool night = garden_is_night(s_state->light);
    if (night != s_night) {
        s_night = night; lv_obj_set_style_image_recolor(g_garden, lv_color_hex(0x142b57), 0);
        lv_obj_set_style_image_recolor_opa(g_garden, night ? 90 : 0, 0);
    }
    for (unsigned i = 0; i < 6; ++i) {
        ui_obj_set_hidden(g_leaves[i], !garden_shared_ready() || garden_state_leaves(s_state) <= i);
        ui_bg_color(g_leaves[i], night ? 0x629b72 : 0xc3ed7a);
    }
    ui_text(g_light, s_state->light == GARDEN_LIGHT_AUTO ? "A" : night ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}
static void garden_water(lv_event_t *e) {
    (void)e; if (!s_visible) return;
    s_state = garden_shared_state();
    if (!garden_shared_ready()) { garden_refresh(); return; }
    uint32_t now = lv_tick_get();
    // 可见回应每次都有；写入以十秒间隔限流，避免连续触摸磨损 Flash。
    // Every tap responds visually; count writes at most once per ten seconds.
    bool count = !s_watered || now - s_hit_at >= 10000;
    s_watered = true; s_hit_at = now;
    if (count && garden_state_water(s_state)) garden_shared_mark_dirty();
    s_saved_at = now; (void)garden_shared_save(); garden_refresh();
}
static void garden_light(lv_event_t *e) {
    (void)e; if (!s_visible || !garden_shared_ready()) return;
    s_state->light = (garden_light_t)((s_state->light + 1) % 3);
    garden_shared_mark_dirty(); (void)garden_shared_save(); garden_refresh();
}
static void garden_tick(void) {
    if (!g_garden || !s_visible) return;
    uint32_t now = lv_tick_get(), elapsed = s_watered ? now - s_hit_at : 2000;
    if (elapsed < 1800) {
        for (int i = 0; i < 6; ++i) {
            uint32_t phase = (elapsed + (uint32_t)i * 95) % 700;
            ui_obj_set_hidden(g_drops[i], elapsed > 1100);
            lv_obj_set_y(g_drops[i], 135 + (int)phase * 130 / 700);
        }
        int size = 12 + (int)(elapsed % 650) * 42 / 650;
        lv_obj_set_size(g_ripple, size, size / 3); lv_obj_set_pos(g_ripple, 254 - size / 2, 294 - size / 6);
        lv_obj_set_style_opa(g_ripple, (lv_opa_t)(210 - 190 * (elapsed % 650) / 650), 0);
        ui_obj_set_hidden(g_ripple, false);
    } else {
        for (int i = 0; i < 6; ++i) ui_obj_set_hidden(g_drops[i], true);
        ui_obj_set_hidden(g_ripple, true);
    }
    if (garden_shared_dirty() && now - s_saved_at >= 10000) { s_saved_at = now; (void)garden_shared_save(); }
    uint32_t key = garden_day_key();
    if (key && key != s_last_day) {
        s_last_day = key;
        if (garden_shared_ready() && s_state->day_key != key) { garden_state_roll_day(s_state, key); garden_shared_mark_dirty(); }
    }
    garden_refresh();
}
static void garden_visibility(bool visible) {
    s_visible = visible;
    if (!visible) { (void)garden_shared_save(); return; }
    // 遮挡期间没有积累动画帧；恢复时直接呈现当前昼夜。
    // Covered time creates no animation backlog; resume at the current lighting.
    s_watered = false; garden_refresh();
}
static void garden_enter(lv_obj_t *parent) {
    s_state = garden_shared_state(); s_visible = true; s_watered = false; s_night = true;
    s_last_day = 0; s_saved_at = lv_tick_get();
    launcher_set_title(garden_words("GARDEN", "生态箱"));
    lv_obj_t *subtitle = garden_label(parent, "POCKET GARDEN", 84, 300, &font_companion_19, 0x7c8584);
    lv_obj_set_style_text_letter_space(subtitle, 3, 0);
    g_garden = lv_image_create(parent); lv_image_set_src(g_garden, &garden_image);
    lv_obj_set_pos(g_garden, (466 - GARDEN_ART_WIDTH) / 2, 105);
    ui_obj_set_clickable(g_garden, true); lv_obj_add_event_cb(g_garden, garden_water, LV_EVENT_CLICKED, NULL);
    static const int leaf_xy[6][2] = {{224, 116}, {245, 133}, {166, 161}, {302, 169}, {185, 191}, {325, 190}};
    for (unsigned i = 0; i < 6; ++i) {
        // 六处新芽可视化累计成长；之后仍继续累计，不会惩罚离开。
        // Six visible shoots express early growth; totals keep growing without absence penalties.
        g_leaves[i] = lv_obj_create(parent); lv_obj_remove_style_all(g_leaves[i]);
        lv_obj_set_pos(g_leaves[i], leaf_xy[i][0], leaf_xy[i][1]); lv_obj_set_size(g_leaves[i], 5, 3);
        lv_obj_set_style_bg_opa(g_leaves[i], LV_OPA_COVER, 0); ui_obj_set_hidden(g_leaves[i], true);
    }
    g_ripple = lv_obj_create(parent); lv_obj_remove_style_all(g_ripple);
    lv_obj_set_style_radius(g_ripple, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_border_width(g_ripple, 1, 0);
    lv_obj_set_style_border_color(g_ripple, lv_color_hex(0x8bebe0), 0); ui_obj_set_hidden(g_ripple, true);
    for (int i = 0; i < 6; ++i) {
        g_drops[i] = lv_obj_create(parent); lv_obj_remove_style_all(g_drops[i]);
        lv_obj_set_size(g_drops[i], 3, 7); lv_obj_set_x(g_drops[i], 150 + i * 28);
        lv_obj_set_style_bg_color(g_drops[i], lv_color_hex(0x80daca), 0); lv_obj_set_style_bg_opa(g_drops[i], 210, 0);
        lv_obj_set_style_radius(g_drops[i], 2, 0); ui_obj_set_hidden(g_drops[i], true);
    }
    g_summary = garden_label(parent, "", 343, 300, &font_companion_20, 0xe8f1e7);
    g_hint = garden_label(parent, "", 373, 310, &font_companion_17, 0x8b9c91);
    g_water = garden_button(parent, 126, 400, 161, garden_words("WATER", "浇水"), garden_water);
    lv_obj_t *light = garden_button(parent, 298, 400, 40, "A", garden_light); g_light = lv_obj_get_child(light, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(g_water, 0), &font_companion_20, 0);
    garden_refresh(); garden_tick();
}
static void garden_exit(void) {
    (void)garden_shared_save(); s_visible = false;
    g_garden = g_summary = g_hint = g_water = g_light = g_ripple = NULL;
    for (int i = 0; i < 6; ++i) g_drops[i] = g_leaves[i] = NULL;
}
const app_t app_garden = {.name = "Garden", .color = COL_TXT, .enter = garden_enter,
    .tick = garden_tick, .exit = garden_exit, .tick_period_ms = 100, .visibility = garden_visibility};
