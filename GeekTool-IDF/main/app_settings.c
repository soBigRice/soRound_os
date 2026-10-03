// 466px 圆屏:每屏一个设置,上下切换;滑块实时生效,松手持久化。
#include "app.h"
#include "settings.h"
#include "watchface.h"
#include "weather_location_ui.h"
#include "weather_artwork.h"
#include "glyph.h"
#include "audio_out.h"
#include "esp_app_desc.h"
#include "img_store.h"
#include "weather_ui.h"
#include <stdio.h>
#include <math.h>

enum { IT_BRIGHT, IT_FACE, IT_AOD, IT_VOL, IT_SILENT, IT_LANG, IT_ABOUT, ITEM_COUNT };
static lv_obj_t *s_parent,*s_panel,*s_value,*s_slider,*s_percent;
static int s_item;
static bool s_audio_ready;
static const char *word(const char *zh,const char *en) {return settings_lang()?zh:en;}
static void rebuild(void *arg);
static void queue_rebuild(void) {lv_async_call_cancel(rebuild,NULL);lv_async_call(rebuild,NULL);}
static lv_obj_t *text(lv_obj_t *p,const char *value,int y,uint32_t color,const lv_font_t *font) {
    lv_obj_t *l=lv_label_create(p);lv_obj_set_style_text_font(l,font,0);
    lv_obj_set_style_text_color(l,lv_color_hex(color),0);lv_label_set_text(l,value);
    lv_obj_set_width(l,316);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);
    lv_label_set_long_mode(l,LV_LABEL_LONG_MODE_DOTS);lv_obj_align(l,LV_ALIGN_TOP_MID,0,y);return l;
}
static void nav(lv_event_t *e) {
    int delta=(int)(intptr_t)lv_event_get_user_data(e);
    s_item=(s_item+delta+ITEM_COUNT)%ITEM_COUNT;queue_rebuild();
}
static void gesture(lv_event_t *e) {
    (void)e;lv_indev_t *indev=lv_indev_active();if(!indev) return;
    lv_dir_t dir=lv_indev_get_gesture_dir(indev);
    if(dir==LV_DIR_TOP || dir==LV_DIR_BOTTOM) {
        s_item=(s_item+(dir==LV_DIR_TOP?1:ITEM_COUNT-1))%ITEM_COUNT;queue_rebuild();
        lv_event_stop_bubbling(e);
    }
}
static lv_obj_t *button(const char *value,int x,int y,int w,int h,lv_event_cb_t cb,int data) {
    lv_obj_t *b=lv_button_create(s_panel);lv_obj_remove_style_all(b);
    lv_obj_set_size(b,w,h);lv_obj_align(b,LV_ALIGN_TOP_MID,x,y);
    lv_obj_set_style_bg_opa(b,LV_OPA_COVER,0);lv_obj_set_style_bg_color(b,lv_color_hex(0x171b1e),0);
    lv_obj_set_style_bg_color(b,lv_color_hex(0x2b3034),LV_STATE_PRESSED);lv_obj_set_style_radius(b,LV_RADIUS_CIRCLE,0);
    lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,(void *)(intptr_t)data);
    lv_obj_t *l=lv_label_create(b);lv_obj_set_style_text_font(l,&font_location_24,0);
    lv_obj_set_style_text_color(l,lv_color_hex(0xf2eee6),0);lv_label_set_text(l,value);lv_obj_center(l);
    if(cb==nav) {
        lv_label_set_text(l,"");
        int mid=h/2, edge=data<0?mid+5:mid-5, tip=data<0?mid-5:mid+5;
        glyph_line(b,w/2-10,edge,w/2,tip,7,2,0xf2eee6);
        glyph_line(b,w/2,tip,w/2+10,edge,7,2,0xf2eee6);
        lv_obj_set_style_bg_opa(b,LV_OPA_TRANSP,0);
    }
    return b;
}
static const char *title(void) {
    static const str_id_t ids[]={S_BRIGHTNESS,S_FACE,S_ALWAYS_ON,S_VOLUME,S_SILENT,S_LANGUAGE,S_ABOUT};
    return tr(ids[s_item]);
}
static void update_number(int value) {
    char b[8];snprintf(b,sizeof b,"%d",value);glyph_digits_set(s_value,b,0xf2eee6,0xf2eee6);lv_obj_align(s_value,LV_ALIGN_TOP_MID,-15,206);
    if(s_percent) lv_obj_align_to(s_percent,s_value,LV_ALIGN_OUT_RIGHT_MID,14,0);
}
static void slider_changed(lv_event_t *e) {
    int v=lv_slider_get_value(lv_event_get_target_obj(e));
    if(s_item==IT_BRIGHT) {settings_set_brightness((uint8_t)v);update_number(v*100/255);}
    else {settings_set_volume((uint8_t)v);audio_out_set_volume((uint8_t)v);update_number(v);}
    lv_obj_invalidate(s_slider);
}
static void save(lv_event_t *e) {(void)e;settings_save();}
static void blip(lv_event_t *e) {(void)e;audio_out_blip();}
static void dotted_slider(lv_event_t *e) {
    lv_obj_t *slider=lv_event_get_target_obj(e);lv_area_t a;lv_obj_get_coords(slider,&a);
    int value=lv_slider_get_value(slider),min=lv_slider_get_min_value(slider),max=lv_slider_get_max_value(slider);
    lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.radius=LV_RADIUS_CIRCLE;d.bg_opa=LV_OPA_COVER;
    for(int i=0;i<47;++i) {
        d.bg_color=lv_color_hex(i* (max-min)<=46*(value-min)?COL_RED:0x343b40);
        int x=a.x1+i*(lv_area_get_width(&a)-1)/46,y=(a.y1+a.y2)/2;
        lv_area_t dot={x-2,y-2,x+2,y+2};lv_draw_rect(lv_event_get_layer(e),&d,&dot);
    }
}
static void toggle(lv_event_t *e) {
    (void)e;
    if(s_item==IT_AOD) settings_set_idle_mode(settings_idle_mode()==IDLE_AOD?IDLE_OFF:IDLE_AOD);
    else settings_set_silent(!settings_silent());
    settings_save();queue_rebuild();
}
static void pick_face(lv_event_t *e) {
    int count=watchface_count(),index=(watchface_selected()+(int)(intptr_t)lv_event_get_user_data(e)+count)%count;
    watchface_select(index);settings_set_face((uint8_t)index);settings_save();queue_rebuild();
}
static void pick_lang(lv_event_t *e) {
    settings_set_lang((uint8_t)(uintptr_t)lv_event_get_user_data(e));settings_save();queue_rebuild();
}
static void draw_face(lv_event_t *e) {
    if(lv_event_get_code(e)!=LV_EVENT_DRAW_MAIN) return;
    lv_area_t a;lv_obj_get_coords(lv_event_get_target_obj(e),&a);
    const weather_artwork_t *art=weather_artwork_for(3,true);if(!art)return;
    const lv_image_dsc_t *source=watchface_selected()==4?img_store_face_image():art->image;
    if(!source)return;
    lv_draw_image_dsc_t d;lv_draw_image_dsc_init(&d);d.src=source;d.pivot=(lv_point_t){0,0};
    // A compact weather preview uses the same source pixels at uniform half scale.
    d.scale_x=d.scale_y=watchface_selected()==4?64:128;
    int left=a.x1+(174-(source->header.w*d.scale_x/256))/2;
    lv_area_t area={left,a.y1,left+source->header.w-1,a.y1+source->header.h-1};
    lv_draw_image(lv_event_get_layer(e),&d,&area);
}
static void rebuild(void *arg) {
    (void)arg;if(!s_parent) return;
    if(s_panel) lv_obj_delete(s_panel);
    s_value=s_slider=s_percent=NULL;
    s_panel=lv_obj_create(s_parent);lv_obj_remove_style_all(s_panel);lv_obj_set_size(s_panel,466,466);
    lv_obj_remove_flag(s_panel,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(s_panel,LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_panel,gesture,LV_EVENT_GESTURE,NULL);
    launcher_set_title(tr_app_name("settings"));
    button("^",0,101,64,42,nav,-1);
    text(s_panel,title(),157,0xf2eee6,&font_location_24);
    if(s_item==IT_BRIGHT || s_item==IT_VOL) {
        if(s_item==IT_VOL && !s_audio_ready) {audio_out_init();s_audio_ready=true;}
        s_value=glyph_digits_create(s_panel,14,5);
        update_number(s_item==IT_BRIGHT?settings_brightness()*100/255:settings_volume());
        s_percent=lv_label_create(s_panel);lv_obj_set_style_text_font(s_percent,&font_location_24,0);
        lv_obj_set_style_text_color(s_percent,lv_color_hex(0x90999f),0);lv_label_set_text(s_percent,"%");
        lv_obj_align_to(s_percent,s_value,LV_ALIGN_OUT_RIGHT_MID,14,0);
        s_slider=lv_slider_create(s_panel);lv_obj_set_size(s_slider,282,32);lv_obj_align(s_slider,LV_ALIGN_TOP_MID,0,317);
        lv_slider_set_range(s_slider,s_item==IT_BRIGHT?SETTINGS_BRIGHT_MIN:0,s_item==IT_BRIGHT?255:100);
        lv_slider_set_value(s_slider,s_item==IT_BRIGHT?settings_brightness():settings_volume(),LV_ANIM_OFF);
        lv_obj_set_style_bg_opa(s_slider,LV_OPA_TRANSP,LV_PART_MAIN);lv_obj_set_style_bg_opa(s_slider,LV_OPA_TRANSP,LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(s_slider,lv_color_hex(0xf2eee6),LV_PART_KNOB);lv_obj_set_style_pad_all(s_slider,0,LV_PART_KNOB);
        lv_obj_set_style_radius(s_slider,LV_RADIUS_CIRCLE,LV_PART_KNOB);
        lv_obj_remove_flag(s_slider,LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_event_cb(s_slider,dotted_slider,LV_EVENT_DRAW_MAIN_BEGIN,NULL);
        lv_obj_add_event_cb(s_slider,slider_changed,LV_EVENT_VALUE_CHANGED,NULL);lv_obj_add_event_cb(s_slider,save,LV_EVENT_RELEASED,NULL);
        if(s_item==IT_VOL) button(word("点击试听","Listen"),0,359,170,42,blip,0);
        else text(s_panel,word("拖动调整","Drag to adjust"),364,COL_TXT2,&font_location_24);
    } else if(s_item==IT_AOD || s_item==IT_SILENT) {
        bool on=s_item==IT_AOD?settings_idle_mode()==IDLE_AOD:settings_silent();
        glyph_arc(s_panel,233,248,43,-.92f,4.06f,12,4,on?0xf2eee6:COL_TXT2);
        glyph_line(s_panel,233,200,233,243,10,4,on?COL_RED:COL_TXT2);
        lv_obj_t *b=button(word(on?"开启":"关闭",on?"ON":"OFF"),0,321,192,60,toggle,0);
        lv_obj_set_style_bg_color(b,lv_color_hex(on?COL_RED:0x23282c),0);
    } else if(s_item==IT_FACE) {
        int selected=watchface_selected();
        if(selected==3 || selected==4) {
            lv_obj_t *preview=lv_obj_create(s_panel);lv_obj_remove_style_all(preview);lv_obj_set_size(preview,174,76);
            lv_obj_set_height(preview,120);lv_obj_align(preview,LV_ALIGN_TOP_MID,0,196);lv_obj_add_event_cb(preview,draw_face,LV_EVENT_DRAW_MAIN,NULL);
            if(selected==4) text(s_panel,"12:34",240,COL_TXT,&lv_font_montserrat_20);
        } else if(selected==1) {
            text(s_panel,"12:34",217,0xf2eee6,&lv_font_montserrat_40);
        } else {
            lv_obj_t *digits=glyph_digits_create(s_panel,10,3);glyph_digits_set(digits,"12:34",0xf2eee6,selected==2?COL_RED:COL_TXT2);
            lv_obj_align(digits,LV_ALIGN_TOP_MID,0,212);
            if(selected==2) glyph_circle(s_panel,233,245,74,16,2,COL_TXT2);
        }
        button("<",-118,305,50,50,pick_face,-1);button(">",118,305,50,50,pick_face,1);
        text(s_panel,watchface_name(selected),315,0xf2eee6,&font_location_24);
        for(int i=0;i<watchface_count();++i) glyph_dot(s_panel,233+(i-(watchface_count()-1)/2)*15,375,3,i==selected?COL_RED:0x343b40);
    } else if(s_item==IT_LANG) {
        lv_obj_t *en=button("English",0,224,260,62,pick_lang,0);
        lv_obj_t *zh=button("中文",0,302,260,62,pick_lang,1);
        lv_obj_set_style_bg_color(settings_lang()?zh:en,lv_color_hex(COL_RED),0);
    } else {
        glyph_circle(s_panel,233,232,40,13,3,0xf2eee6);glyph_dot(s_panel,233,232,5,COL_RED);
        text(s_panel,"soRound OS",290,0xf2eee6,&font_location_24);
        text(s_panel,esp_app_get_description()->version,328,COL_TXT2,&font_weather_16);
        text(s_panel,"ESP32-S3 / 466",363,COL_TXT2,&font_location_24);
    }
    button("v",0,405,64,36,nav,1);
    for(int i=0;i<ITEM_COUNT;++i) glyph_dot(s_panel,203+i*10,448,2,i==s_item?COL_RED:0x343b40);
}
static void settings_enter(lv_obj_t *parent) {s_parent=parent;s_item=IT_BRIGHT;s_audio_ready=false;rebuild(NULL);}
static void settings_exit(void) {
    lv_async_call_cancel(rebuild,NULL);if(s_audio_ready) audio_out_deinit();
    s_parent=s_panel=s_value=s_slider=s_percent=NULL;s_audio_ready=false;
}
const app_t app_settings={"settings",COL_TXT,settings_enter,NULL,settings_exit};
