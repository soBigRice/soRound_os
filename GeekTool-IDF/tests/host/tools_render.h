#pragma once
#include "tools_ui.h"
// Same LVGL battery geometry as launcher_init; 74% is a screenshot fixture, never firmware data.
static inline void tools_test_battery(void) {
    lv_obj_t *ring=lv_arc_create(lv_layer_top());
    lv_obj_set_size(ring,458,458);lv_obj_center(ring);
    lv_arc_set_rotation(ring,270);lv_arc_set_bg_angles(ring,0,360);
    lv_arc_set_range(ring,0,100);lv_arc_set_value(ring,74);
    lv_obj_remove_style(ring,NULL,LV_PART_KNOB);ui_obj_set_clickable(ring,false);
    lv_obj_set_style_arc_width(ring,8,LV_PART_MAIN);lv_obj_set_style_arc_width(ring,8,LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ring,lv_color_hex(TOOLS_FAINT),LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring,lv_color_hex(TOOLS_WHITE),LV_PART_INDICATOR);
}
