#include "app.h"
#include "lvgl_compat.h"
#include "audio_out.h"
#include "buttons.h"
#include "merit_store.h"
#include "settings.h"
#include <stdio.h>
#include <math.h>

LV_FONT_DECLARE(font_location_24);
static lv_obj_t *g_wood,*g_count,*g_plus,*g_hint,*g_sound;
static bool s_visible,s_dirty,s_muted,s_storage_ready;
static uint32_t s_count,s_hit_at,s_saved_at,s_anim_at,s_elapsed;
static const char *words(const char *en,const char *zh){return settings_lang()?zh:en;}
static lv_obj_t *label(lv_obj_t *p,const char *value,int y,const lv_font_t *font,uint32_t color) {
    lv_obj_t *o=lv_label_create(p);lv_obj_set_style_text_font(o,font,0);
    lv_obj_set_style_text_color(o,lv_color_hex(color),0);lv_label_set_text(o,value);
    lv_obj_set_width(o,340);lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);lv_obj_align(o,LV_ALIGN_TOP_MID,0,y);return o;
}
static void rect(lv_layer_t *layer,lv_obj_t *o,int x,int y,int w,int h,int radius,uint32_t color,int border) {
    lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.base.obj=o;
    d.bg_color=lv_color_hex(color);d.bg_opa=border?LV_OPA_TRANSP:LV_OPA_COVER;
    d.border_width=border;d.border_color=lv_color_hex(color);d.border_opa=LV_OPA_COVER;d.radius=radius;
    lv_area_t a={x,y,x+w-1,y+h-1};lv_draw_rect(layer,&d,&a);
}
static void stroke(lv_layer_t *layer,lv_obj_t *o,int x,int y,int x2,int y2,int width,uint32_t color) {
    lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);d.base.obj=o;d.width=width;
    d.color=lv_color_hex(color);d.round_start=d.round_end=true;
    d.p1=(lv_point_precise_t){x,y};d.p2=(lv_point_precise_t){x2,y2};lv_draw_line(layer,&d);
}
static void wood_draw(lv_event_t *e) {
    lv_obj_t *o=lv_event_get_target_obj(e);lv_layer_t *layer=lv_event_get_layer(e);
    lv_area_t a;lv_obj_get_coords(o,&a);int x=a.x1,y=a.y1;
    float t=s_elapsed/360.0f;
    int bounce=s_elapsed<360?(int)lroundf(6*sinf(t*3.14159265f)*expf(-2*t)):0;
    rect(layer,o,x+29,y+64+bounce,170,112,56,0x2c1f18,0);
    rect(layer,o,x+29,y+64+bounce,170,112,56,0xe3c6a0,3);
    rect(layer,o,x+49,y+84+bounce,130,74,36,0x9b7650,2);
    stroke(layer,o,x+77,y+67+bounce,x+126,y+131+bounce,10,0x090807);
    rect(layer,o,x+112,y+119+bounce,22,22,11,0x090807,0);
    // The striker approaches the slit on every accepted tap; geometry stays inside its hit target.
    int lift=s_elapsed<360?(int)lroundf(18*(1-expf(-7*t))*expf(-2*t)):0;
    stroke(layer,o,x+146,y+42-lift,x+207,y+7-lift,8,0xd8bb94);
    rect(layer,o,x+131,y+27-lift,30,30,15,s_elapsed<120?COL_RED:0xf1d8b4,0);
}
static void update_count(void) {
    char value[32];snprintf(value,sizeof value,"%lu",(unsigned long)s_count);lv_label_set_text(g_count,value);
    lv_obj_set_style_text_font(g_count,s_count>999999?&font_location_24:&lv_font_montserrat_40,0);
}
static void save(void) {
    if(!s_dirty)return;
    if(merit_save(s_count)){s_dirty=false;lv_label_set_text(g_hint,words("TAP THE WOODEN FISH","点木鱼 · 攒功德"));}
    else lv_label_set_text(g_hint,words("SAVE FAILED · RETRY","保存失败 · 稍后重试"));
    s_saved_at=lv_tick_get();
}
static void knock(lv_event_t *e) {
    (void)e;if(!g_wood||!s_visible)return;
    if(!s_storage_ready) {
        s_storage_ready=merit_load(&s_count);
        if(s_storage_ready){update_count();lv_label_set_text(g_hint,words("TAP THE WOODEN FISH","点木鱼 · 攒功德"));}
        return;
    }
    if(s_count==MERIT_MAX)return;
    ++s_count;s_dirty=true;s_hit_at=s_anim_at=lv_tick_get();s_elapsed=0;
    update_count();lv_label_set_text(g_plus,"+1");
    lv_obj_set_style_opa(g_plus,255,0);lv_obj_align(g_plus,LV_ALIGN_TOP_MID,0,207);
    lv_obj_invalidate(g_wood);if(!s_muted&&!settings_silent())audio_out_knock();
}
static void sound_label(void) {
    lv_label_set_text(g_sound,settings_silent()?words("SYSTEM MUTE","系统静音"):
        s_muted?words("SOUND OFF","声音关闭"):words("SOUND ON","声音开启"));
}
static void sound(lv_event_t *e) {
    (void)e;if(settings_silent())return;s_muted=!s_muted;
    sound_label();
}
static void merit_tick(void) {
    if(!g_wood||!s_visible)return;
    if(buttons_control_pressed())knock(NULL);
    uint32_t now=lv_tick_get();
    if(s_elapsed<360) {
        uint32_t delta=now-s_anim_at;s_anim_at=now;
        s_elapsed+=delta>360-s_elapsed?360-s_elapsed:delta;
        lv_obj_set_style_opa(g_plus,255-255*s_elapsed/360,0);
        lv_obj_align(g_plus,LV_ALIGN_TOP_MID,0,207-20*s_elapsed/360);lv_obj_invalidate(g_wood);
    }
    // Coalesce writes after the tap burst; never write Flash for every hit.
    if(s_dirty && now-s_saved_at>=2000 && (now-s_hit_at>=2000 || now-s_saved_at>=20000))save();
}
static void merit_visibility(bool visible) {
    s_visible=visible;buttons_reset_control();s_anim_at=lv_tick_get();
    if(visible){sound_label();audio_out_init();}else{audio_out_deinit();save();}
}
static void merit_enter(lv_obj_t *parent) {
    if(!s_dirty){s_count=0;s_storage_ready=merit_load(&s_count);}
    s_visible=true;s_muted=false;
    s_elapsed=360;s_hit_at=s_saved_at=s_anim_at=lv_tick_get();buttons_reset_control();audio_out_init();
    label(parent,words("ACCUMULATED MERIT","累计功德"),109,&font_location_24,COL_TXT2);
    g_count=label(parent,"0",145,&lv_font_montserrat_40,COL_TXT);update_count();
    if(!s_storage_ready)lv_label_set_text(g_count,"--");
    g_plus=label(parent,"",207,&font_location_24,COL_RED);
    g_wood=lv_obj_create(parent);lv_obj_remove_style_all(g_wood);lv_obj_set_size(g_wood,230,192);
    lv_obj_set_pos(g_wood,118,199);ui_obj_set_scrollable(g_wood,false);ui_obj_set_clickable(g_wood,true);
    lv_obj_add_event_cb(g_wood,wood_draw,LV_EVENT_DRAW_MAIN,NULL);lv_obj_add_event_cb(g_wood,knock,LV_EVENT_CLICKED,NULL);
    g_hint=label(parent,s_storage_ready?words("TAP THE WOODEN FISH","点木鱼 · 攒功德"):words("TAP TO RETRY LOAD","点击重试读取"),377,&font_location_24,0xa0a0a6);
    lv_obj_set_width(g_hint,280);
    g_sound=label(parent,s_muted?words("SOUND OFF","声音关闭"):words("SOUND ON","声音开启"),408,&font_location_24,0xa0a0a6);
    lv_obj_set_width(g_sound,180);
    sound_label();
    ui_obj_set_clickable(g_sound,true);lv_obj_add_event_cb(g_sound,sound,LV_EVENT_CLICKED,NULL);
}
static void merit_exit(void) {
    save();s_visible=false;audio_out_deinit();buttons_reset_control();
    g_wood=g_count=g_plus=g_hint=g_sound=NULL;
}
const app_t app_merit={.name="Merit",.color=COL_TXT,.enter=merit_enter,.tick=merit_tick,.exit=merit_exit,
    .visibility=merit_visibility,.tick_period_ms=20};
