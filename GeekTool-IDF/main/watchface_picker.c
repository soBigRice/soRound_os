#include "watchface_picker.h"
#include "control_ui.h"
#include "settings.h"
#include "watchface.h"
#include "watchface_ui.h"
#include "watchface_studio_draw.h"
#include <stdio.h>

static void picker_rebuild(void *arg);
static watchface_picker_t *picker_from_event(lv_event_t *event) {
    lv_obj_t *panel=lv_obj_get_parent(lv_event_get_target_obj(event));
    watchface_picker_t *picker=lv_obj_get_user_data(panel);
    return picker && picker->panel==panel?picker:NULL;
}
static void picker_queue(watchface_picker_t *picker) {
    // 按钮回调结束后再重建，避免在事件派发中销毁当前按钮。
    // Rebuild after the button callback so the dispatch target stays alive.
    lv_async_call_cancel(picker_rebuild,picker);lv_async_call(picker_rebuild,picker);
}
static void picker_face(lv_event_t *event) {
    watchface_picker_t *picker=picker_from_event(event);if(!picker)return;
    int count=watchface_count();
    int index=(watchface_selected()+(int)(intptr_t)lv_event_get_user_data(event)+count)%count;
    watchface_select(index);settings_set_face((uint8_t)index);settings_save();picker_queue(picker);
}
static void picker_theme(lv_event_t *event) {
    watchface_picker_t *picker=picker_from_event(event);if(!picker)return;
    int theme=(int)(intptr_t)lv_event_get_user_data(event);
    int index=watchface_index_in_theme(theme,watchface_selected());
    watchface_select(index);settings_set_face((uint8_t)index);settings_save();picker_queue(picker);
}
static void picker_rebuild(void *arg) {
    watchface_picker_t *picker=arg;if(!picker->panel)return;
    lv_obj_t *panel=picker->panel;picker->preview=NULL;lv_obj_clean(panel);
    int selected=watchface_selected();
    picker->preview=watchface_create_preview(panel,selected);lv_obj_set_pos(picker->preview,116,116);
    lv_obj_t *prev=control_button(panel,52,205,50,56,picker_face,(void *)(intptr_t)-1);
    lv_obj_t *next=control_button(panel,364,205,50,56,picker_face,(void *)(intptr_t)1);
    lv_obj_t *label=control_label(prev,LV_SYMBOL_LEFT,UI_FONT_SYM,0,0,30,CONTROL_WHITE);lv_obj_center(label);
    label=control_label(next,LV_SYMBOL_RIGHT,UI_FONT_SYM,0,0,30,CONTROL_WHITE);lv_obj_center(label);
    char caption[64];snprintf(caption,sizeof caption,"%s   %d / %d",watchface_kind_name(selected),selected+1,watchface_count());
    label=control_label(panel,caption,control_small_font(),94,354,278,CONTROL_WHITE);
    lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
    lv_label_set_long_mode(label,LV_LABEL_LONG_MODE_WRAP);lv_obj_set_height(label,LV_SIZE_CONTENT);
    // 与原设置页相同的233px预览和五组触摸区，不增加对象递归深度。
    // Keep the original 233px preview and five group targets without adding object recursion.
    enum { THEME_WIDTH=54, THEME_GAP=4,
           THEME_ROW_WIDTH=WATCHFACE_THEME_COUNT*THEME_WIDTH+(WATCHFACE_THEME_COUNT-1)*THEME_GAP };
    for(int theme=0;theme<WATCHFACE_THEME_COUNT;++theme) {
        int x=(466-THEME_ROW_WIDTH)/2+theme*(THEME_WIDTH+THEME_GAP);
        lv_obj_t *button=control_button(panel,x,377,THEME_WIDTH,40,picker_theme,(void *)(intptr_t)theme);
        lv_obj_set_style_radius(button,12,0);
        bool active=theme==watchface_theme_for(selected);
        if(active){lv_obj_set_style_border_width(button,1,0);lv_obj_set_style_border_color(button,lv_color_hex(COL_RED),0);}
        label=control_label(button,watchface_theme_name(theme),&font_wf_14,0,0,THEME_WIDTH,active?CONTROL_WHITE:CONTROL_GRAY);
        lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(label);
    }
}
static void picker_deleted(lv_event_t *event) {
    watchface_picker_t *picker=lv_event_get_user_data(event);
    if(picker->panel!=lv_event_get_target_obj(event))return;
    lv_async_call_cancel(picker_rebuild,picker);picker->panel=picker->preview=NULL;
}
void watchface_picker_unmount(watchface_picker_t *picker) {
    wfs_geometry_release();
    lv_async_call_cancel(picker_rebuild,picker);
    if(picker->panel) {
        lv_obj_remove_event_cb_with_user_data(picker->panel,picker_deleted,picker);
        lv_obj_set_user_data(picker->panel,NULL);
    }
    picker->panel=picker->preview=NULL;
}
void watchface_picker_mount(watchface_picker_t *picker, lv_obj_t *panel) {
    watchface_picker_unmount(picker);picker->panel=panel;
    // 专用panel保存实例指针，两个入口各自持有状态，选择和NVS仍共用原服务。
    // The dedicated panel identifies its view instance; both entries share the existing selection and NVS services.
    lv_obj_set_user_data(panel,picker);lv_obj_add_event_cb(panel,picker_deleted,LV_EVENT_DELETE,picker);
    picker_rebuild(picker);
}
void watchface_picker_refresh(watchface_picker_t *picker) {
    if(picker->preview)watchface_refresh_preview(picker->preview);
}
