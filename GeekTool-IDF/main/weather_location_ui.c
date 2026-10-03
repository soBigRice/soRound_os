#include "weather_location_ui.h"
#include "weather_locations.h"
#include "app.h"
#include "settings.h"
#include "weather_ui.h"
#include "lvgl_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static lv_obj_t *root, *common, *picker, *roller, *path, *confirm, *common_caption;
static bool (*on_select)(uint16_t);
static uint16_t trail[3], options[64];
static size_t option_count;
static unsigned stage;
static const char *word(const char *zh,const char *en) { return settings_lang()?zh:en; }
static lv_obj_t *label(lv_obj_t *p,const char *text,int y,uint32_t color) {
    lv_obj_t *l=lv_label_create(p);
    lv_obj_set_style_text_font(l,&font_location_24,0);
    lv_obj_set_style_text_color(l,lv_color_hex(color),0);
    lv_label_set_text(l,text); lv_obj_set_width(l,322);
    lv_label_set_long_mode(l,LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_align(l,LV_ALIGN_TOP_MID,0,y); return l;
}
static lv_obj_t *button(lv_obj_t *p,const char *text,int y,lv_event_cb_t cb,void *data) {
    lv_obj_t *b=lv_button_create(p); lv_obj_remove_style_all(b);
    lv_obj_set_size(b,300,62); lv_obj_align(b,LV_ALIGN_TOP_MID,0,y);
    lv_obj_set_style_bg_opa(b,LV_OPA_COVER,0);
    lv_obj_set_style_bg_color(b,lv_color_hex(0x15191c),0);
    lv_obj_set_style_bg_color(b,lv_color_hex(0x292d30),LV_STATE_PRESSED);
    lv_obj_set_style_radius(b,24,0);
    lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,data);
    lv_obj_t *l=label(b,text,16,0xf2eee6); lv_obj_set_width(l,280);
    return b;
}
static void update_path(void) {
    char text[200]="";
    for(unsigned i=0;i<=stage;++i) if(trail[i]) {
        if(i) strncat(text," / ",sizeof text-strlen(text)-1);
        strncat(text,wx_location_name(trail[i],settings_lang()),sizeof text-strlen(text)-1);
    }
    lv_label_set_text(path,text);
    const char *names[]={word("省份","Province"),word("城市","City"),word("区县","District")};
    launcher_set_title(names[stage]);
}
static void roller_changed(lv_event_t *e) {
    (void)e; unsigned selected=lv_roller_get_selected(roller);
    if(selected<option_count) trail[stage]=options[selected];
    update_path();
}
static void load_stage(void) {
    uint16_t parent=stage?trail[stage-1]:0;
    option_count=wx_location_children(parent,options,64);
    // Offline source guarantees at most 64 siblings. Never silently truncate.
    if(!option_count || option_count>64) { lv_label_set_text(path,word("地址数据不可用","Location unavailable")); return; }
    size_t bytes=1;
    for(size_t i=0;i<option_count;++i) bytes+=strlen(wx_location_name(options[i],settings_lang()))+1;
    char *text=malloc(bytes);
    if(!text) {lv_label_set_text(path,word("地址数据不可用","Location unavailable"));return;}
    text[0]=0; unsigned selected=0;
    for(size_t i=0;i<option_count;++i) {
        if(i) strcat(text,"\n");
        strcat(text,wx_location_name(options[i],settings_lang()));
        if(options[i]==trail[stage]) selected=(unsigned)i;
    }
    lv_roller_set_options(roller,text,LV_ROLLER_MODE_NORMAL); free(text);
    lv_roller_set_selected(roller,selected,LV_ANIM_OFF); trail[stage]=options[selected];
    update_path();
    lv_label_set_text(lv_obj_get_child(confirm,0),word(stage==2?"确认地址":"下一步",stage==2?"Confirm":"Next"));
}
static void commit(uint16_t index) {
    if(on_select && on_select(index)) {
        // Retire after the input event; no deletion of the active event target.
        lv_obj_delete_async(root); root=common=picker=roller=path=confirm=NULL;
    } else lv_label_set_text(ui_obj_is_hidden(picker)?common_caption:path,word("保存失败，请重试","Save failed. Try again."));
}
static void next(lv_event_t *e) {
    (void)e;
    if(!trail[stage]) return;
    if(stage<2 && wx_location_children(trail[stage],NULL,0)) {++stage;load_stage();}
    else commit(trail[stage]);
}
static void quick(lv_event_t *e) {commit((uint16_t)(uintptr_t)lv_event_get_user_data(e));}
static void other(lv_event_t *e) {
    (void)e; ui_obj_set_hidden(common,true); ui_obj_set_hidden(picker,false); stage=0; load_stage();
}
void weather_location_ui_open(lv_obj_t *parent,bool (*select)(uint16_t)) {
    weather_location_ui_close(); on_select=select;
    root=lv_obj_create(parent);lv_obj_remove_style_all(root);lv_obj_set_size(root,466,466);
    lv_obj_set_style_bg_color(root,lv_color_hex(0),0);lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);
    lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(root,LV_OBJ_FLAG_GESTURE_BUBBLE);
    common=lv_obj_create(root);lv_obj_remove_style_all(common);lv_obj_set_size(common,466,466);ui_obj_set_scrollable(common,false);
    picker=lv_obj_create(root);lv_obj_remove_style_all(picker);lv_obj_set_size(picker,466,466);ui_obj_set_scrollable(picker,false);ui_obj_set_hidden(picker,true);
    launcher_set_title(word("选择地址","Location"));
    common_caption=label(common,word("最近 / 常用","Recent / Common"),112,COL_TXT2);
    uint16_t recent[3];size_t n=wx_location_recent(recent);
    const uint32_t defaults[]={3101,1101,3301};
    for(unsigned j=0;j<3 && n<3;++j) {
        uint16_t i=wx_location_find(defaults[j]);bool duplicate=false;
        for(size_t k=0;k<n;++k) if(recent[k]==i) duplicate=true;
        if(i && !duplicate) recent[n++]=i;
    }
    for(size_t i=0;i<n;++i) {
        uint16_t province=recent[i];
        while(wx_locations[province].parent) province=wx_locations[province].parent;
        lv_obj_t *b=button(common,wx_location_name(recent[i],settings_lang()),158+(int)i*66,quick,(void *)(uintptr_t)recent[i]);
        if(province!=recent[i] && strcmp(wx_location_name(province,true),wx_location_name(recent[i],true))) {
            lv_obj_align(lv_obj_get_child(b,0),LV_ALIGN_TOP_MID,0,4);
            lv_obj_t *context=label(b,wx_location_name(province,settings_lang()),34,COL_TXT2);
            lv_obj_set_style_text_font(context,&font_weather_16,0);lv_obj_set_width(context,280);
        }
    }
    button(common,word("其他地点","Other location"),356,other,NULL);
    path=label(picker,"",113,COL_TXT2);
    roller=lv_roller_create(picker);lv_obj_set_width(roller,326);
    lv_obj_set_style_text_font(roller,&font_location_24,LV_PART_MAIN);
    lv_obj_set_style_text_font(roller,&font_location_24,LV_PART_SELECTED);
    lv_obj_set_style_text_color(roller,lv_color_hex(0x727b80),LV_PART_MAIN);
    lv_obj_set_style_text_color(roller,lv_color_hex(0xf2eee6),LV_PART_SELECTED);
    lv_obj_set_style_bg_color(roller,lv_color_hex(0),LV_PART_MAIN);
    lv_obj_set_style_bg_color(roller,lv_color_hex(0x171a1e),LV_PART_SELECTED);
    lv_obj_set_style_border_width(roller,0,0);lv_obj_set_style_radius(roller,24,0);
    lv_obj_set_style_text_line_space(roller,22,0);lv_roller_set_visible_row_count(roller,3);
    lv_obj_align(roller,LV_ALIGN_TOP_MID,0,183);lv_obj_remove_flag(roller,LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(roller,roller_changed,LV_EVENT_VALUE_CHANGED,NULL);
    confirm=button(picker,word("下一步","Next"),356,next,NULL);
    uint16_t index=wx_location_selected(); memset(trail,0,sizeof trail);
    for(int i=2;i>=0 && index;--i) {trail[i]=index;index=wx_locations[index].parent;}
    if(!trail[0]) {trail[0]=trail[1];trail[1]=trail[2];trail[2]=0;}
}
bool weather_location_ui_back(void) {
    if(!root) return false;
    if(!ui_obj_is_hidden(picker)) {
        if(stage) {--stage;load_stage();}
        else {ui_obj_set_hidden(picker,true);ui_obj_set_hidden(common,false);launcher_set_title(word("选择地址","Location"));}
    } else weather_location_ui_close();
    return true;
}
void weather_location_ui_close(void) {
    if(root) lv_obj_delete(root);
    root=common=picker=roller=path=confirm=NULL;on_select=NULL;
}
bool weather_location_ui_visible(void) {return root!=NULL;}
