// 系统设置 —— 为 466px 圆屏设计的三级信息架构:
//   首页 = 显示 / 声音 / 系统三枚分类胶囊;
//   分类 = 本组独立设置行;
//   详情 = 大读数滑块、大开关或选择列表。
// 返回:详情 → 分类 → 首页 → 启动器。所有动画只在入场时短暂执行,无常驻刷新。
#include "app.h"
#include "settings.h"
#include "watchface.h"
#include "glyph.h"
#include "audio_out.h"
#include "esp_app_desc.h"
#include <stdio.h>

#define HUB_ROW_W       318
#define HUB_FOCUS_W     350
#define HUB_ROW_H        72
#define HUB_FOCUS_H      82
#define GROUP_ROW_W     318
#define GROUP_ROW_H      70
#define CARD_BG      0x141418
#define FOCUS_BG     0x25252a
#define BORDER_COL   0x34343a
#define FOCUS_BORDER 0x626268
#define UI_FONT_XL   &lv_font_montserrat_40

enum { IT_BRIGHT, IT_FACE, IT_AOD, IT_VOL, IT_SILENT, IT_LANG, IT_ABOUT };
enum { GR_DISPLAY, GR_SOUND, GR_SYSTEM };
enum { LEVEL_HUB, LEVEL_GROUP, LEVEL_DETAIL };

static lv_obj_t *s_hub, *s_group, *s_detail, *s_dval;
static int s_level = LEVEL_HUB;
static int s_group_id = GR_DISPLAY;
static bool s_audio_ready;

static void build_hub(void);
static void build_group(int group);
static void open_detail(int id);

/* ===================== 轻量入场动画 ===================== */
static void anim_y_exec(void *var, int32_t v) {
    lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0);
}

static void anim_opa_exec(void *var, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void animate_in(lv_obj_t *obj, uint32_t delay, uint32_t duration) {
    lv_anim_delete(obj, anim_y_exec);
    lv_anim_delete(obj, anim_opa_exec);
    lv_obj_set_style_translate_y(obj, 12, 0);
    lv_obj_set_style_opa(obj, LV_OPA_TRANSP, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, anim_y_exec);
    lv_anim_set_values(&a, 12, 0);
    lv_anim_set_duration(&a, duration);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, anim_opa_exec);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&a, duration);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void show_panel(lv_obj_t *target) {
    lv_obj_add_flag(s_hub, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_detail, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(target, LV_OBJ_FLAG_HIDDEN);
}

/* ===================== 设置回调(实时生效,松手或点选写 NVS) ===================== */
static void on_released(lv_event_t *e) { (void)e; settings_save(); }

static void br_changed(lv_event_t *e) {
    int v = lv_slider_get_value(lv_event_get_target_obj(e));
    settings_set_brightness((uint8_t)v);
    if (s_dval) {
        char b[8];
        snprintf(b, sizeof b, "%d%%", v * 100 / 255);
        lv_label_set_text(s_dval, b);
    }
}

static void vol_changed(lv_event_t *e) {
    int v = lv_slider_get_value(lv_event_get_target_obj(e));
    settings_set_volume((uint8_t)v);
    audio_out_set_volume((uint8_t)v);
    if (s_dval) {
        char b[8];
        snprintf(b, sizeof b, "%d%%", v);
        lv_label_set_text(s_dval, b);
    }
}

static void vol_blip(lv_event_t *e) { (void)e; audio_out_blip(); }

static void aod_changed(lv_event_t *e) {
    bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    settings_set_idle_mode(on ? IDLE_AOD : IDLE_OFF);
    settings_save();
}

static void silent_changed(lv_event_t *e) {
    bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    settings_set_silent(on ? 1 : 0);
    settings_save();
}

/* ===================== 首页:三分类胶囊 ===================== */
static void draw_display_icon(lv_obj_t *row, int cx, int cy, uint32_t color) {
    glyph_circle(row, cx, cy, 11, 7, 2, color);
    static const int rays[8][4] = {
        { 0,-22, 0,-17}, { 0,17, 0,22}, {-22, 0,-17, 0}, {17, 0,22, 0},
        {-16,-16,-12,-12}, {12,12,16,16}, {-16,16,-12,12}, {12,-12,16,-16}
    };
    for (int i = 0; i < 8; i++) {
        glyph_line(row, cx + rays[i][0], cy + rays[i][1],
                   cx + rays[i][2], cy + rays[i][3], 4, 2, color);
    }
}

static void draw_sound_icon(lv_obj_t *row, int cx, int cy, uint32_t color) {
    glyph_line(row, cx - 18, cy - 7, cx - 9, cy - 7, 4, 2, color);
    glyph_line(row, cx - 18, cy + 7, cx - 9, cy + 7, 4, 2, color);
    glyph_line(row, cx - 18, cy - 7, cx - 18, cy + 7, 4, 2, color);
    glyph_line(row, cx - 9, cy - 7, cx + 1, cy - 16, 4, 2, color);
    glyph_line(row, cx - 9, cy + 7, cx + 1, cy + 16, 4, 2, color);
    glyph_line(row, cx + 1, cy - 16, cx + 1, cy + 16, 4, 2, color);
    glyph_arc(row, cx + 1, cy, 14, -0.96f, 0.96f, 5, 2, color);
    glyph_arc(row, cx + 1, cy, 22, -0.84f, 0.84f, 5, 2, color);
}

static void draw_system_icon(lv_obj_t *row, int cx, int cy, uint32_t color) {
    glyph_circle(row, cx, cy, 9, 6, 2, color);
    glyph_circle(row, cx, cy, 20, 8, 2, color);
    for (int i = -1; i <= 1; i++) {
        int d = i * 11;
        glyph_line(row, cx - 22, cy + d, cx - 14, cy + d, 4, 2, color);
        glyph_line(row, cx + 14, cy + d, cx + 22, cy + d, 4, 2, color);
    }
}

static void group_click(lv_event_t *e) {
    build_group((int)(intptr_t)lv_event_get_user_data(e));
}

static lv_obj_t *hub_row(const char *title, const char *summary, int group,
                         int y, bool focused) {
    lv_obj_t *row = lv_obj_create(s_hub);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, focused ? HUB_FOCUS_W : HUB_ROW_W,
                    focused ? HUB_FOCUS_H : HUB_ROW_H);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_radius(row, 25, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(focused ? FOCUS_BG : 0x070708), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(focused ? FOCUS_BORDER : BORDER_COL), 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x303036), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(row, lv_color_hex(COL_RED), LV_STATE_PRESSED);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(row, group_click, LV_EVENT_CLICKED, (void *)(intptr_t)group);

    int icon_x = focused ? 58 : 48;
    int cy = (focused ? HUB_FOCUS_H : HUB_ROW_H) / 2;
    if (group == GR_DISPLAY) draw_display_icon(row, icon_x, cy, COL_TXT);
    else if (group == GR_SOUND) draw_sound_icon(row, icon_x, cy, COL_TXT);
    else draw_system_icon(row, icon_x, cy, COL_TXT);

    if (focused) glyph_dot(row, 18, cy, 3, COL_RED);

    lv_obj_t *t = lv_label_create(row);
    lv_obj_set_style_text_font(t, UI_FONT_L, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(COL_TXT), 0);
    lv_label_set_text(t, title);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, focused ? 103 : 91, -12);

    lv_obj_t *v = lv_label_create(row);
    lv_obj_set_style_text_font(v, UI_FONT_M, 0);
    lv_obj_set_style_text_color(v, lv_color_hex(focused ? 0xb8b8bd : COL_TXT2), 0);
    lv_label_set_text(v, summary);
    lv_obj_align(v, LV_ALIGN_LEFT_MID, focused ? 103 : 91, 16);

    lv_obj_t *ch = lv_label_create(row);
    lv_obj_set_style_text_font(ch, UI_FONT_SYM, 0);
    lv_obj_set_style_text_color(ch, lv_color_hex(focused ? COL_TXT : COL_TXT2), 0);
    lv_label_set_text(ch, LV_SYMBOL_RIGHT);
    lv_obj_align(ch, LV_ALIGN_RIGHT_MID, -20, 0);
    return row;
}

static void build_hub(void) {
    lv_obj_clean(s_hub);
    char display[48], sound[48], system[48];
    snprintf(display, sizeof display, "%d%%  %s",
             settings_brightness() * 100 / 255,
             watchface_name(watchface_selected()));
    snprintf(sound, sizeof sound, "%d%%  %s",
             settings_volume(), tr(settings_silent() ? S_OFF : S_ON));
    snprintf(system, sizeof system, "%s  %s",
             settings_lang() ? "中文" : "English", tr(S_ABOUT));

    lv_obj_t *rows[3];
    rows[0] = hub_row(tr(S_DISPLAY), display, GR_DISPLAY, 110, false);
    rows[1] = hub_row(tr(S_SOUND), sound, GR_SOUND, 190, true);
    rows[2] = hub_row(tr(S_SYSTEM), system, GR_SYSTEM, 280, false);
    for (int i = 0; i < 3; i++) animate_in(rows[i], (uint32_t)i * 40, 190);

    s_level = LEVEL_HUB;
    show_panel(s_hub);
    launcher_set_title(tr_app_name("settings"));
}

/* ===================== 分类页:独立设置行 ===================== */
static void item_click(lv_event_t *e) {
    open_detail((int)(intptr_t)lv_event_get_user_data(e));
}

static lv_obj_t *group_row(const char *title, const char *value, int id, int y) {
    lv_obj_t *row = lv_obj_create(s_group);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, GROUP_ROW_W, GROUP_ROW_H);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_radius(row, 22, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(CARD_BG), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(BORDER_COL), 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(FOCUS_BG), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(row, lv_color_hex(COL_RED), LV_STATE_PRESSED);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(row, item_click, LV_EVENT_CLICKED, (void *)(intptr_t)id);

    glyph_dot(row, 22, GROUP_ROW_H / 2, 3, id == IT_ABOUT ? COL_TXT2 : COL_RED);

    lv_obj_t *t = lv_label_create(row);
    lv_obj_set_style_text_font(t, UI_FONT_L, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(COL_TXT), 0);
    lv_label_set_text(t, title);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 39, 0);

    lv_obj_t *ch = lv_label_create(row);
    lv_obj_set_style_text_font(ch, UI_FONT_SYM, 0);
    lv_obj_set_style_text_color(ch, lv_color_hex(COL_TXT2), 0);
    lv_label_set_text(ch, LV_SYMBOL_RIGHT);
    lv_obj_align(ch, LV_ALIGN_RIGHT_MID, -18, 0);

    if (value && value[0]) {
        lv_obj_t *v = lv_label_create(row);
        lv_obj_set_style_text_font(v, UI_FONT_M, 0);
        lv_obj_set_style_text_color(v, lv_color_hex(0xaaaab0), 0);
        lv_label_set_text(v, value);
        lv_obj_align(v, LV_ALIGN_RIGHT_MID, -46, 0);
    }
    return row;
}

static void build_group(int group) {
    lv_obj_clean(s_group);
    s_group_id = group;
    char b0[24];
    lv_obj_t *rows[3] = {0};
    int count = 0;

    if (group == GR_DISPLAY) {
        snprintf(b0, sizeof b0, "%d%%", settings_brightness() * 100 / 255);
        rows[count++] = group_row(tr(S_BRIGHTNESS), b0, IT_BRIGHT, 116);
        rows[count++] = group_row(tr(S_FACE), watchface_name(watchface_selected()), IT_FACE, 196);
        rows[count++] = group_row(tr(S_ALWAYS_ON),
                                  tr(settings_idle_mode() == IDLE_AOD ? S_ON : S_OFF),
                                  IT_AOD, 276);
        launcher_set_title(tr(S_DISPLAY));
    } else if (group == GR_SOUND) {
        snprintf(b0, sizeof b0, "%d%%", settings_volume());
        rows[count++] = group_row(tr(S_VOLUME), b0, IT_VOL, 156);
        rows[count++] = group_row(tr(S_SILENT),
                                  tr(settings_silent() ? S_ON : S_OFF), IT_SILENT, 236);
        launcher_set_title(tr(S_SOUND));
    } else {
        rows[count++] = group_row(tr(S_LANGUAGE),
                                  settings_lang() ? "中文" : "English", IT_LANG, 156);
        rows[count++] = group_row(tr(S_ABOUT), "", IT_ABOUT, 236);
        launcher_set_title(tr(S_SYSTEM));
    }

    for (int i = 0; i < count; i++) animate_in(rows[i], (uint32_t)i * 40, 190);
    s_level = LEVEL_GROUP;
    show_panel(s_group);
}

/* ===================== 详情页:单控件全屏 ===================== */
static void style_slider(lv_obj_t *slider) {
    lv_obj_set_height(slider, 18);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x27272d), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COL_RED), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COL_TXT), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 8, LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, 2, LV_PART_KNOB);
    lv_obj_set_style_border_color(slider, lv_color_hex(COL_RED), LV_PART_KNOB);
}

static void style_switch(lv_obj_t *sw) {
    lv_obj_set_style_bg_color(sw, lv_color_hex(0x29292f), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, lv_color_hex(COL_RED), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, lv_color_hex(COL_TXT), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_KNOB);
}

static void build_slider_detail(int mn, int mx, int val,
                                lv_event_cb_t chg, bool is_vol) {
    s_dval = lv_label_create(s_detail);
    lv_obj_set_style_text_font(s_dval, UI_FONT_XL, 0);
    lv_obj_set_style_text_color(s_dval, lv_color_hex(COL_TXT), 0);
    lv_obj_align(s_dval, LV_ALIGN_CENTER, 0, -44);
    char b[8];
    snprintf(b, sizeof b, "%d%%", is_vol ? val : val * 100 / 255);
    lv_label_set_text(s_dval, b);

    lv_obj_t *sl = lv_slider_create(s_detail);
    lv_obj_set_width(sl, 292);
    style_slider(sl);
    lv_slider_set_range(sl, mn, mx);
    lv_slider_set_value(sl, val, LV_ANIM_OFF);
    lv_obj_align(sl, LV_ALIGN_CENTER, 0, 50);
    lv_obj_remove_flag(sl, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(sl, chg, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(sl, on_released, LV_EVENT_RELEASED, NULL);
    if (is_vol) lv_obj_add_event_cb(sl, vol_blip, LV_EVENT_RELEASED, NULL);

    lv_obj_t *hint = lv_label_create(s_detail);
    lv_obj_set_style_text_font(hint, UI_FONT_M, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(COL_TXT2), 0);
    lv_label_set_text(hint, is_vol ? tr(S_VOLUME) : tr(S_BRIGHTNESS));
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 98);
}

static void build_switch_detail(const char *desc, bool on, lv_event_cb_t cb) {
    lv_obj_t *sw = lv_switch_create(s_detail);
    lv_obj_set_size(sw, 96, 48);
    style_switch(sw);
    lv_obj_align(sw, LV_ALIGN_CENTER, 0, -24);
    lv_obj_remove_flag(sw, LV_OBJ_FLAG_GESTURE_BUBBLE);
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *d = lv_label_create(s_detail);
    lv_obj_set_style_text_font(d, UI_FONT_M, 0);
    lv_obj_set_style_text_color(d, lv_color_hex(COL_TXT2), 0);
    lv_obj_set_style_text_align(d, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(d, 300);
    lv_label_set_text(d, desc);
    lv_obj_align(d, LV_ALIGN_CENTER, 0, 58);
}

// 列表原地刷新选中高亮:不在自身点击回调里 clean 父容器,避免 LVGL 对象生命周期隐患。
static void list_highlight(lv_obj_t *list, int sel) {
    int n = (int)lv_obj_get_child_count(list);
    for (int i = 0; i < n; i++) {
        lv_obj_t *it = lv_obj_get_child(list, i);
        lv_obj_set_style_bg_color(it, lv_color_hex(i == sel ? 0x2a1216 : CARD_BG), 0);
        lv_obj_set_style_border_color(it, lv_color_hex(i == sel ? COL_RED : BORDER_COL), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(it, 0),
                                    lv_color_hex(i == sel ? COL_RED : COL_TXT), 0);
    }
}

static void lang_pick(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    settings_set_lang((uint8_t)idx);
    settings_save();
    list_highlight(lv_obj_get_parent(lv_event_get_target_obj(e)), idx);
    launcher_set_title(tr(S_LANGUAGE));
}

static void face_pick(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    watchface_select(idx);
    settings_set_face((uint8_t)idx);
    settings_save();
    list_highlight(lv_obj_get_parent(lv_event_get_target_obj(e)), idx);
}

static lv_obj_t *make_choice_list(int height, int y) {
    lv_obj_t *list = lv_obj_create(s_detail);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 300, height);
    lv_obj_align(list, LV_ALIGN_CENTER, 0, y);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list, 8, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(list, LV_OBJ_FLAG_EVENT_BUBBLE);
    return list;
}

static lv_obj_t *choice_row(lv_obj_t *list, const char *text, bool selected,
                            lv_event_cb_t cb, int idx) {
    lv_obj_t *it = lv_obj_create(list);
    lv_obj_remove_style_all(it);
    lv_obj_set_size(it, 250, 56);
    lv_obj_set_style_radius(it, 18, 0);
    lv_obj_set_style_bg_color(it, lv_color_hex(selected ? 0x2a1216 : CARD_BG), 0);
    lv_obj_set_style_bg_opa(it, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(it, 1, 0);
    lv_obj_set_style_border_color(it, lv_color_hex(selected ? COL_RED : BORDER_COL), 0);
    lv_obj_set_style_bg_color(it, lv_color_hex(FOCUS_BG), LV_STATE_PRESSED);
    lv_obj_add_flag(it, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(it, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(it, cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
    lv_obj_t *l = lv_label_create(it);
    lv_obj_set_style_text_font(l, UI_FONT_L, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(selected ? COL_RED : COL_TXT), 0);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    return it;
}

static void build_face_detail(void) {
    lv_obj_t *list = make_choice_list(298, 25);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    int cur = watchface_selected(), n = watchface_count();
    for (int i = 0; i < n; i++) choice_row(list, watchface_name(i), i == cur, face_pick, i);
}

static void build_lang_detail(void) {
    lv_obj_t *list = make_choice_list(210, 20);
    lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    static const char *const LANGS[2] = { "English", "中文" };
    int cur = settings_lang() ? 1 : 0;
    for (int i = 0; i < 2; i++) choice_row(list, LANGS[i], i == cur, lang_pick, i);
}

static void build_about_detail(void) {
    int cx = 233, ly = 150;
    glyph_circle(s_detail, cx, ly, 44, 13, 3, COL_TXT);
    glyph_circle(s_detail, cx, ly, 28, 12, 3, COL_TXT);
    glyph_dot(s_detail, cx, ly, 6, COL_RED);

    lv_obj_t *row = lv_obj_create(s_detail);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 232);
    lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_t *n1 = lv_label_create(row);
    lv_obj_set_style_text_font(n1, UI_FONT_SYM, 0);
    lv_obj_set_style_text_color(n1, lv_color_hex(COL_TXT), 0);
    lv_label_set_text(n1, "soRound");
    lv_obj_t *n2 = lv_label_create(row);
    lv_obj_set_style_text_font(n2, UI_FONT_SYM, 0);
    lv_obj_set_style_text_color(n2, lv_color_hex(COL_RED), 0);
    lv_label_set_text(n2, "OS");

    const esp_app_desc_t *ad = esp_app_get_description();
    char vb[40];
    snprintf(vb, sizeof vb, "version %s", ad->version);
    lv_obj_t *ver = lv_label_create(s_detail);
    lv_obj_set_style_text_font(ver, UI_FONT_M, 0);
    lv_obj_set_style_text_color(ver, lv_color_hex(COL_TXT2), 0);
    lv_label_set_text(ver, vb);
    lv_obj_align(ver, LV_ALIGN_TOP_MID, 0, 278);

    lv_obj_t *dev = lv_label_create(s_detail);
    lv_obj_set_style_text_font(dev, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(dev, lv_color_hex(COL_TXT2), 0);
    lv_label_set_text(dev, "ESP32-S3  /  466 round");
    lv_obj_align(dev, LV_ALIGN_TOP_MID, 0, 306);
}

static const char *detail_title(int id) {
    switch (id) {
        case IT_BRIGHT: return tr(S_BRIGHTNESS);
        case IT_FACE: return tr(S_FACE);
        case IT_AOD: return tr(S_ALWAYS_ON);
        case IT_VOL: return tr(S_VOLUME);
        case IT_SILENT: return tr(S_SILENT);
        case IT_LANG: return tr(S_LANGUAGE);
        case IT_ABOUT: return tr(S_ABOUT);
        default: return tr_app_name("settings");
    }
}

static void open_detail(int id) {
    lv_obj_clean(s_detail);
    s_dval = NULL;
    if (id == IT_VOL && !s_audio_ready) {
        audio_out_init();
        s_audio_ready = true;
    }

    switch (id) {
        case IT_BRIGHT:
            build_slider_detail(SETTINGS_BRIGHT_MIN, 0xFF, settings_brightness(), br_changed, false);
            break;
        case IT_VOL:
            build_slider_detail(0, 100, settings_volume(), vol_changed, true);
            break;
        case IT_AOD:
            build_switch_detail(tr(S_AOD_DESC), settings_idle_mode() == IDLE_AOD, aod_changed);
            break;
        case IT_SILENT:
            build_switch_detail(tr(S_SILENT_DESC), settings_silent(), silent_changed);
            break;
        case IT_FACE: build_face_detail(); break;
        case IT_LANG: build_lang_detail(); break;
        case IT_ABOUT: build_about_detail(); break;
    }

    s_level = LEVEL_DETAIL;
    show_panel(s_detail);
    animate_in(s_detail, 0, 180);
    launcher_set_title(detail_title(id));
}

// 框架返回回调:详情 → 分类,分类 → 首页,首页返 false 让框架退出 app。
static bool settings_back(void) {
    if (s_level == LEVEL_DETAIL) {
        build_group(s_group_id);
        return true;
    }
    if (s_level == LEVEL_GROUP) {
        build_hub();
        return true;
    }
    return false;
}

/* ===================== 生命周期 ===================== */
static lv_obj_t *make_panel(lv_obj_t *parent) {
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_EVENT_BUBBLE);
    return panel;
}

static void settings_enter(lv_obj_t *parent) {
    s_hub = make_panel(parent);
    s_group = make_panel(parent);
    s_detail = make_panel(parent);
    lv_obj_add_flag(s_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_detail, LV_OBJ_FLAG_HIDDEN);

    s_level = LEVEL_HUB;
    s_group_id = GR_DISPLAY;
    s_dval = NULL;
    s_audio_ready = false;
    build_hub();
}

static void settings_exit(void) {
    if (s_audio_ready) audio_out_deinit();
    s_hub = s_group = s_detail = s_dval = NULL;
    s_level = LEVEL_HUB;
    s_group_id = GR_DISPLAY;
    s_audio_ready = false;
}

const app_t app_settings = { "settings", COL_TXT, settings_enter, NULL, settings_exit, settings_back };
