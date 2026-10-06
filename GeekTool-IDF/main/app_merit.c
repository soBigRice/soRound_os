#include "app.h"
#include "lvgl_compat.h"
#include "audio_out.h"
#include "buttons.h"
#include "merit_store.h"
#include "settings.h"
#include "merit_artwork.h"
#include "esp_heap_caps.h"
#include "jpeg_decoder.h"
#include "esp_err.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

LV_FONT_DECLARE(font_location_24);
static lv_obj_t *g_wood,*g_count,*g_plus,*g_hint,*g_sound;
static bool s_visible,s_dirty,s_muted,s_storage_ready;
static uint32_t s_count,s_hit_at,s_saved_at,s_anim_at,s_elapsed;
static lv_image_dsc_t s_art;
#define HIT_MS 620
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
    float hit=s_elapsed<260?expf(-s_elapsed/65.0f):0;
    int bounce=(int)lroundf(3*hit);
    if(s_art.data) {
        lv_draw_image_dsc_t d;lv_draw_image_dsc_init(&d);d.base.obj=o;d.src=&s_art;
        lv_area_t bounds={x+11,y+8+bounce,x+11+MERIT_ART_WIDTH-1,y+8+bounce+MERIT_ART_HEIGHT-1};
        lv_draw_image(layer,&d,&bounds);
    }
    // A tap starts at contact; the mallet then rebounds without recoloring the wood.
    int strike=(int)lroundf(23*hit);
    stroke(layer,o,x+196-strike/2,y+28+strike,x+268-strike/2,y+7+strike,9,0xb98a58);
    stroke(layer,o,x+197-strike/2,y+26+strike,x+268-strike/2,y+5+strike,3,0xe4c397);
    rect(layer,o,x+177-strike/2,y+12+strike,35,28,14,0x946536,0);
    rect(layer,o,x+180-strike/2,y+13+strike,28,22,11,0xd4ac76,0);
}
static void load_artwork(void) {
    size_t bytes=(size_t)MERIT_ART_WIDTH*MERIT_ART_HEIGHT*2;
    uint8_t *pixels=heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM);
    if(!pixels)return;
    esp_jpeg_image_cfg_t cfg={.indata=(uint8_t *)merit_jpeg,.indata_size=(uint32_t)merit_jpeg_size,
        .outbuf=pixels,.outbuf_size=(uint32_t)bytes,.out_format=JPEG_IMAGE_FORMAT_RGB565,
        .out_scale=JPEG_IMAGE_SCALE_0,.flags={.swap_color_bytes=0}};
    esp_jpeg_image_output_t info={0};
    if(esp_jpeg_decode(&cfg,&info)!=ESP_OK||info.width!=MERIT_ART_WIDTH||info.height!=MERIT_ART_HEIGHT) {
        free(pixels);return;
    }
    s_art=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,
        .w=MERIT_ART_WIDTH,.h=MERIT_ART_HEIGHT,.stride=MERIT_ART_WIDTH*2},.data=pixels,.data_size=bytes};
}
static void update_count(void) {
    char value[32];snprintf(value,sizeof value,"%lu",(unsigned long)s_count);lv_label_set_text(g_count,value);
    lv_obj_set_style_text_font(g_count,s_count>999999?&font_location_24:&lv_font_montserrat_40,0);
}
static const char *idle_hint(void) {
    return s_art.data?words("TAP TO KNOCK","点木鱼 · 攒功德"):words("IMAGE UNAVAILABLE","图片加载失败");
}
static void save(void) {
    if(!s_dirty)return;
    if(merit_save(s_count)){s_dirty=false;lv_label_set_text(g_hint,idle_hint());}
    else lv_label_set_text(g_hint,words("SAVE FAILED · RETRY","保存失败 · 稍后重试"));
    s_saved_at=lv_tick_get();
}
static void knock(lv_event_t *e) {
    (void)e;if(!g_wood||!s_visible)return;
    if(!s_storage_ready) {
        s_storage_ready=merit_load(&s_count);
        if(s_storage_ready){update_count();lv_label_set_text(g_hint,idle_hint());}
        return;
    }
    if(s_count==MERIT_MAX)return;
    ++s_count;s_dirty=true;s_hit_at=s_anim_at=lv_tick_get();s_elapsed=0;
    update_count();lv_label_set_text(g_plus,words("MERIT +1","功德+1"));
    lv_obj_set_style_opa(g_plus,255,0);lv_obj_align(g_plus,LV_ALIGN_TOP_MID,0,183);
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
    if(s_elapsed<HIT_MS) {
        uint32_t delta=now-s_anim_at;s_anim_at=now;
        s_elapsed+=delta>HIT_MS-s_elapsed?HIT_MS-s_elapsed:delta;
        // Hold the complete phrase before fading so it remains legible after a short tap.
        unsigned fade=s_elapsed>200?s_elapsed-200:0;
        lv_obj_set_style_opa(g_plus,255-255*fade/(HIT_MS-200),0);
        lv_obj_align(g_plus,LV_ALIGN_TOP_MID,0,183-18*s_elapsed/HIT_MS);lv_obj_invalidate(g_wood);
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
    s_elapsed=HIT_MS;s_hit_at=s_saved_at=s_anim_at=lv_tick_get();buttons_reset_control();audio_out_init();load_artwork();
    label(parent,words("ACCUMULATED MERIT","累计功德"),102,&font_location_24,0x9e8d77);
    g_count=label(parent,"0",138,&lv_font_montserrat_40,0xf2dfc1);update_count();
    if(!s_storage_ready)lv_label_set_text(g_count,"--");
    g_plus=label(parent,"",183,&font_location_24,0xe5c38d);
    g_wood=lv_obj_create(parent);lv_obj_remove_style_all(g_wood);lv_obj_set_size(g_wood,310,204);
    lv_obj_set_pos(g_wood,78,198);ui_obj_set_scrollable(g_wood,false);ui_obj_set_clickable(g_wood,true);
    lv_obj_add_event_cb(g_wood,wood_draw,LV_EVENT_DRAW_MAIN,NULL);lv_obj_add_event_cb(g_wood,knock,LV_EVENT_CLICKED,NULL);
    g_hint=label(parent,s_storage_ready?idle_hint():words("RETRY LOAD","点击重试读取"),391,&font_location_24,0xa39683);
    lv_obj_set_width(g_hint,268);
    g_sound=label(parent,s_muted?words("SOUND OFF","声音关闭"):words("SOUND ON","声音开启"),418,&font_location_24,0xa0a0a6);
    lv_obj_set_width(g_sound,170);
    sound_label();
    ui_obj_set_clickable(g_sound,true);lv_obj_add_event_cb(g_sound,sound,LV_EVENT_CLICKED,NULL);
}
static void merit_exit(void) {
    save();s_visible=false;audio_out_deinit();buttons_reset_control();
    if(s_art.data){lv_image_cache_drop(&s_art);free((void *)s_art.data);s_art=(lv_image_dsc_t){0};}
    g_wood=g_count=g_plus=g_hint=g_sound=NULL;
}
const app_t app_merit={.name="Merit",.color=COL_TXT,.enter=merit_enter,.tick=merit_tick,.exit=merit_exit,
    .visibility=merit_visibility,.tick_period_ms=20};
