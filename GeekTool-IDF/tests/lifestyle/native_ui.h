#pragma once
#include "app.h"
#include "settings.h"
#include "src/misc/lv_text_private.h"
#include "../host/tools_render.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
LV_FONT_DECLARE(font_location_24);
static uint8_t language;
static unsigned flushed;
uint8_t settings_lang(void){return language;}
_Alignas(LV_DRAW_BUF_ALIGN) static uint16_t buffer[466*40];
static uint16_t pixels[466*466];
static lv_display_t *display;
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *map){int w=lv_area_get_width(a);flushed+=(unsigned)(w*lv_area_get_height(a));for(int y=a->y1;y<=a->y2;++y){memcpy(pixels+y*466+a->x1,map,(size_t)w*2);map+=w*2;}lv_display_flush_ready(d);}
static void init(void){lv_init();i18n_init();display=lv_display_create(466,466);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);lv_display_set_buffers(display,buffer,NULL,sizeof buffer,LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);lv_obj_set_style_bg_color(lv_screen_active(),lv_color_hex(COL_BG),0);tools_test_battery();}
static lv_obj_t *page(const char *name){lv_obj_t *p=lv_obj_create(lv_screen_active());lv_obj_remove_style_all(p);lv_obj_set_size(p,466,466);ui_obj_set_scrollable(p,false);lv_obj_t *t=lv_label_create(lv_layer_top());lv_obj_set_style_text_font(t,&font_location_24,0);lv_obj_set_style_text_color(t,lv_color_hex(COL_TXT),0);lv_label_set_text(t,name);lv_obj_align(t,LV_ALIGN_TOP_MID,0,52);return p;}
static void check_labels(lv_obj_t *o){if(ui_obj_is_hidden(o))return;if(lv_obj_check_type(o,&lv_label_class)){const char *v=lv_label_get_text(o);uint32_t at=0;while(v[at]){uint32_t cp=lv_text_encoded_next(v,&at);if(cp=='\n')continue;lv_font_glyph_dsc_t d;if(!lv_font_get_glyph_dsc(lv_obj_get_style_text_font(o,0),&d,cp,0)||d.is_placeholder){fprintf(stderr,"missing glyph U+%04x: %s\n",(unsigned)cp,v);assert(false);}}}for(uint32_t i=0;i<lv_obj_get_child_count(o);++i)check_labels(lv_obj_get_child(o,i));}
static void circle(lv_obj_t *o){lv_area_t a;lv_obj_get_coords(o,&a);for(int i=0;i<4;++i){int x=i&1?a.x2:a.x1,y=i&2?a.y2:a.y1;if(hypot(x-232.5,y-232.5)>232)fprintf(stderr,"outside circle (%d,%d)-(%d,%d)\n",a.x1,a.y1,a.x2,a.y2);assert(hypot(x-232.5,y-232.5)<=232);}}
static void capture(const char *dir,const char *name){lv_obj_update_layout(lv_screen_active());lv_obj_update_layout(lv_layer_top());check_labels(lv_screen_active());check_labels(lv_layer_top());lv_refr_now(display);if(!dir)return;char path[1024];snprintf(path,sizeof path,"%s/%s-%s.ppm",dir,name,language?"zh":"en");FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n466 466\n255\n");for(unsigned i=0;i<466*466;++i){uint16_t p=pixels[i];uint8_t rgb[]={((p>>11)&31)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};fwrite(rgb,1,3,f);}assert(fclose(f)==0);}
static void tap(lv_obj_t *o){lv_obj_send_event(o,LV_EVENT_CLICKED,NULL);}
