#include "tools_ui.h"
#include "settings.h"

const char *tools_text(const char *en, const char *zh) { return settings_lang() ? zh : en; }

lv_obj_t *tools_surface(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y); lv_obj_set_size(obj, w, h);
    ui_obj_set_scrollable(obj,false);ui_obj_set_clickable(obj,false);
    ui_obj_set_event_bubble(obj,true);
    return obj;
}

void tools_label_center(lv_obj_t *label, int cx, int cy) {
    lv_obj_update_layout(label);
    // Launcher labels previously use TOP_MID; set_pos otherwise remains an aligned offset.
    lv_obj_set_align(label, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(label, cx - lv_obj_get_width(label) / 2, cy - lv_obj_get_height(label) / 2);
}

void tools_label_baseline(lv_obj_t *label, int x, int baseline) {
    const lv_font_t *font = lv_obj_get_style_text_font(label, 0);
    lv_obj_set_align(label, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(label, x, baseline - (font->line_height - font->base_line));
}

lv_obj_t *tools_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                     int cx, int cy, uint32_t color, int spacing) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(label, spacing, 0);
    lv_label_set_text(label, text);
    tools_label_center(label, cx, cy);
    return label;
}

void tools_dot(lv_layer_t *layer, int x, int y, int diameter, uint32_t color) {
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE; d.bg_color = lv_color_hex(color); d.bg_opa = LV_OPA_COVER;
    lv_area_t a = {x - diameter/2, y - diameter/2, x - diameter/2 + diameter-1, y - diameter/2 + diameter-1};
    lv_draw_rect(layer, &d, &a);
}

void tools_circle(lv_layer_t *layer, int x, int y, int radius, int width, uint32_t color) {
    lv_draw_arc_dsc_t d; lv_draw_arc_dsc_init(&d);
    d.center = (lv_point_t){x,y}; d.radius = radius + (width+1)/2;
    d.width = width; d.color = lv_color_hex(color); d.start_angle = 0; d.end_angle = 360;
    lv_draw_arc(layer, &d);
}

void tools_line(lv_layer_t *layer, int x1, int y1, int x2, int y2, int width, uint32_t color) {
    lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d);
    d.p1 = (lv_point_precise_t){x1,y1}; d.p2 = (lv_point_precise_t){x2,y2};
    d.width = width; d.color = lv_color_hex(color); d.round_start = d.round_end = true;
    lv_draw_line(layer, &d);
}

static void fault_draw(lv_event_t *e) {
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a; lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    int cx = a.x1 + 50, cy = a.y1 + 50;
    tools_circle(layer, cx, cy, 48, 2, TOOLS_DIM);
    // The approved '--' mark has two five-dot strokes, no numeric zero for invalid data.
    for (int k = 0; k < 2; ++k) for (int i = 0; i < 5; ++i)
        tools_dot(layer, cx - 35 + k*42 + i*7, cy - 1, 5, COL_RED);
}

lv_obj_t *tools_fault(lv_obj_t *parent, bool audio) {
    lv_obj_t *group = tools_surface(parent, 0, 0, 466, 466);
    lv_obj_t *mark = tools_surface(group, 183, 171, 100, 100);
    lv_obj_add_event_cb(mark, fault_draw, LV_EVENT_DRAW_MAIN, NULL);
    tools_label(group, audio ? tools_text("MIC UNAVAILABLE", "麦克风不可用") :
                tools_text("SENSOR UNAVAILABLE", "传感器不可用"), &font_tools_22, 233, 321, TOOLS_WHITE, 0);
    tools_label(group, tools_text("Waiting for input", "等待数据恢复"), &font_tools_20, 233, 357, TOOLS_GRAY, 0);
    ui_obj_set_hidden(group,true);
    return group;
}

void tools_header(lv_obj_t *title, lv_obj_t *back, lv_obj_t *arrow) {
    lv_obj_set_style_text_font(title, &font_tools_24, 0);
    lv_obj_set_style_text_letter_space(title, 2, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(TOOLS_WHITE), 0);
    lv_obj_set_width(title, LV_SIZE_CONTENT);
    tools_label_center(title, 233, 63);
    lv_obj_set_size(back, 46, 46); lv_obj_set_align(back, LV_ALIGN_TOP_LEFT); lv_obj_set_pos(back, 110, 41);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x16161a), 0);
    lv_obj_set_style_border_width(back, 1, 0);
    lv_obj_set_style_border_color(back, lv_color_hex(TOOLS_DIM), 0);
    lv_obj_set_style_border_opa(back, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(arrow, &font_tools_36, 0);
    lv_obj_set_style_text_color(arrow, lv_color_hex(TOOLS_WHITE), 0);
    lv_label_set_text(arrow, "‹"); tools_label_center(arrow, 23, 20);
}
