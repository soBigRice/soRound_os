#include "weather_refresh.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static weather_refresh_t refresh;
static unsigned waits,sends,old_path;
static uint8_t panel[WEATHER_FRAME_BYTES];
_Alignas(LV_DRAW_BUF_ALIGN) static uint8_t first[466*40*2],second[466*40*2];
static void wait_te(void *arg){assert(arg==&refresh&&refresh.frame.dirty);++waits;}
static void send(void *arg,weather_frame_area_t a,uint8_t *p){assert(arg==&refresh&&waits);assert(a.x1%2==0&&a.y1%2==0&&a.x2%2==1&&a.y2%2==1);++sends;int w=(a.x2-a.x1+1)*2;assert((a.y2-a.y1+1)*w<=(int)sizeof first);for(int y=a.y1;y<=a.y2;++y)memcpy(panel+(y*466+a.x1)*2,p+(y-a.y1)*w,(size_t)w);}
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p){if(!weather_refresh_flush(&refresh,d,a,p)){++old_path;lv_display_flush_ready(d);}}
static void rounder(lv_event_t *e){lv_area_t *a=lv_event_get_param(e);a->x1&=~1;a->y1&=~1;a->x2|=1;a->y2|=1;}
int main(void){
 assert(WEATHER_FRAME_BYTES==434312);refresh.frame.pixels=calloc(1,WEATHER_FRAME_BYTES);assert(refresh.frame.pixels);
 uint8_t tile[32]={1,2,3,4,5,6,7,8},out[32]={0};weather_frame_area_t a={2,4,3,5};
 assert(weather_frame_patch(&refresh.frame,a,tile,8));assert(weather_frame_read(&refresh.frame,a,out,8));assert(!memcmp(out,tile,4)&&!memcmp(out+4,tile+8,4));
 assert(!weather_frame_patch(&refresh.frame,(weather_frame_area_t){-1,0,2,1},tile,8));assert(!weather_frame_read(&refresh.frame,a,out,7));
 memset(refresh.frame.pixels,0,WEATHER_FRAME_BYTES);refresh.frame.dirty=false;refresh.wait_te=wait_te;refresh.send=send;refresh.arg=&refresh;
 lv_init();lv_display_t *d=lv_display_create(466,466);lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565_SWAPPED);lv_display_set_buffers(d,first,second,sizeof first,LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(d,flush);lv_display_add_event_cb(d,rounder,LV_EVENT_INVALIDATE_AREA,NULL);
 lv_obj_t *screen=lv_screen_active();lv_obj_set_style_bg_color(screen,lv_color_hex(0x123456),0);lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);lv_refr_now(d);
 assert(waits==1&&sends==12&&!old_path&&!refresh.frame.dirty&&!memcmp(panel,refresh.frame.pixels,sizeof panel));
 uint8_t original[8];memcpy(original,panel,sizeof original);unsigned sent=sends;
 lv_obj_t *o=lv_obj_create(screen);lv_obj_remove_style_all(o);lv_obj_set_pos(o,102,104);lv_obj_set_size(o,30,26);lv_obj_set_style_bg_color(o,lv_color_hex(0xff0000),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_refr_now(d);
 assert(waits==2&&sends>sent&&!memcmp(panel,original,sizeof original)&&!memcmp(panel,refresh.frame.pixels,sizeof panel));
 sent=sends;lv_refr_now(d);assert(waits==2&&sends==sent);
 free(refresh.frame.pixels);refresh.frame.pixels=NULL;lv_obj_invalidate(screen);lv_refr_now(d);assert(old_path==12&&waits==2&&sends==sent);lv_deinit();
 puts("weather refresh: real LVGL double-buffer/40-line flushes, complete-frame pixels before TE, one wait per frame, bounded staging, unchanged RGB565 byte order, dirty unions, idle and original-path fallback passed");}
