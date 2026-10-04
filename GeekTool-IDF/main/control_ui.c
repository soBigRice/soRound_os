#include "control_ui.h"

const lv_font_t *control_small_font(void) {
    static lv_font_t font;
    font=font_control_18; font.fallback=&font_location_24;
    return &font;
}
lv_obj_t *control_surface(lv_obj_t *parent,int x,int y,int width,int height) {
    lv_obj_t *o=lv_obj_create(parent);lv_obj_remove_style_all(o);
    lv_obj_set_pos(o,x,y);lv_obj_set_size(o,width,height);
    ui_obj_set_scrollable(o,false);ui_obj_set_gesture_bubble(o,true);
    return o;
}
lv_obj_t *control_label(lv_obj_t *parent,const char *text,const lv_font_t *font,
                        int x,int y,int width,uint32_t color) {
    lv_obj_t *o=lv_label_create(parent);lv_obj_set_style_text_font(o,font,0);
    lv_obj_set_style_text_color(o,lv_color_hex(color),0);lv_label_set_text(o,text);
    lv_obj_set_width(o,width);lv_label_set_long_mode(o,LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(o,font->line_height);
    lv_obj_set_pos(o,x,y);ui_obj_set_clickable(o,false);ui_obj_set_gesture_bubble(o,true);
    return o;
}
lv_obj_t *control_button(lv_obj_t *parent,int x,int y,int width,int height,
                         lv_event_cb_t callback,void *data) {
    lv_obj_t *o=lv_button_create(parent);lv_obj_remove_style_all(o);
    lv_obj_set_pos(o,x,y);lv_obj_set_size(o,width,height);
    lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_bg_color(o,lv_color_hex(CONTROL_CARD),0);
    lv_obj_set_style_bg_color(o,lv_color_hex(0x303036),LV_STATE_PRESSED);
    lv_obj_set_style_radius(o,18,0);ui_obj_set_scrollable(o,false);ui_obj_set_gesture_bubble(o,true);
    if(callback)lv_obj_add_event_cb(o,callback,LV_EVENT_CLICKED,data);
    return o;
}
void control_button_text(lv_obj_t *button,const char *text) {
    // New objects have no computed width until layout; a negative label width would wrap every letter.
    lv_obj_update_layout(button);
    lv_obj_t *o=control_label(button,text,&font_location_24,0,0,lv_obj_get_width(button)-16,CONTROL_WHITE);
    lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(o);
}
lv_obj_t *control_slider(lv_obj_t *parent,int y,int min,int max,int value,
                         lv_event_cb_t changed,lv_event_cb_t released) {
    lv_obj_t *o=lv_slider_create(parent);lv_obj_set_size(o,286,36);lv_obj_align(o,LV_ALIGN_TOP_MID,0,y);
    lv_slider_set_range(o,min,max);lv_slider_set_value(o,value,LV_ANIM_OFF);
    lv_obj_set_style_bg_color(o,lv_color_hex(CONTROL_LINE),LV_PART_MAIN);
    lv_obj_set_style_bg_color(o,lv_color_hex(COL_RED),LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(o,lv_color_hex(CONTROL_WHITE),LV_PART_KNOB);
    lv_obj_set_style_pad_ver(o,12,LV_PART_MAIN);lv_obj_set_style_pad_all(o,5,LV_PART_KNOB);
    lv_obj_set_style_radius(o,LV_RADIUS_CIRCLE,LV_PART_MAIN);
    lv_obj_set_style_radius(o,LV_RADIUS_CIRCLE,LV_PART_INDICATOR);
    ui_obj_set_gesture_bubble(o,false);
    lv_obj_add_event_cb(o,changed,LV_EVENT_VALUE_CHANGED,NULL);
    lv_obj_add_event_cb(o,released,LV_EVENT_RELEASED,NULL);return o;
}
static void toggle_row(lv_event_t *e) {
    lv_obj_t *sw=lv_event_get_user_data(e);
    if(lv_obj_has_state(sw,LV_STATE_CHECKED))lv_obj_remove_state(sw,LV_STATE_CHECKED);
    else lv_obj_add_state(sw,LV_STATE_CHECKED);
    lv_obj_send_event(sw,LV_EVENT_VALUE_CHANGED,NULL);
}
lv_obj_t *control_toggle(lv_obj_t *parent,int x,int y,int width,
                         const char *title,const char *detail,bool on,
                         lv_event_cb_t changed,void *data) {
    lv_obj_t *row=control_button(parent,x,y,width,64,NULL,NULL);
    control_label(row,title,&font_location_24,18,5,width-100,CONTROL_WHITE);
    control_label(row,detail,control_small_font(),18,36,width-100,CONTROL_GRAY);
    lv_obj_t *sw=lv_switch_create(row);lv_obj_set_size(sw,58,32);lv_obj_set_pos(sw,width-76,16);
    lv_obj_set_style_bg_color(sw,lv_color_hex(CONTROL_LINE),LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw,lv_color_hex(COL_RED),LV_PART_INDICATOR|LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw,lv_color_hex(CONTROL_WHITE),LV_PART_KNOB);
    ui_obj_set_gesture_bubble(sw,false);ui_obj_set_event_bubble(sw,false);
    if(on)lv_obj_add_state(sw,LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw,changed,LV_EVENT_VALUE_CHANGED,data);
    lv_obj_add_event_cb(row,toggle_row,LV_EVENT_CLICKED,sw);return sw;
}
