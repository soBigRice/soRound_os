// Render the real capsule LVGL page; state fixtures never touch a device.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "tools_ui.h"
#include "capsule_recorder.h"
#include "../tests/host/tools_render.h"
#include "src/misc/lv_text_private.h"
static uint8_t language;
void launcher_set_title(const char *title){assert(title);}
uint8_t settings_lang(void){return language;}
static capsule_snapshot_t fixture;
static unsigned recordings,stops,plays,deletes,enters,exits;
void capsule_recorder_enter(void){++enters;}
void capsule_recorder_exit(void){++exits;}
bool capsule_recorder_record(void){++recordings;return true;}
bool capsule_recorder_stop(void){++stops;return true;}
bool capsule_recorder_play(const char *name){assert(capsule_name_valid(name));++plays;return true;}
bool capsule_recorder_delete(const char *name){assert(capsule_name_valid(name));++deletes;return true;}
void capsule_recorder_snapshot(capsule_snapshot_t *snapshot){*snapshot=fixture;}
const lv_font_t *i18n_font_sym(void){return &lv_font_montserrat_20;}
#include "../main/app_capsules.c"
_Alignas(LV_DRAW_BUF_ALIGN) static uint16_t buffer[466*466];
_Alignas(LV_DRAW_BUF_ALIGN) static uint16_t second_buffer[466*40];
static uint16_t pixels[466*466],reference_pixels[466*466];
static bool device_format;
static lv_display_t *display;
static unsigned flushed_pixels;
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *map) {
    int width=lv_area_get_width(a);
    flushed_pixels+=(unsigned)(width*lv_area_get_height(a));
    for (int y=a->y1;y<=a->y2;++y) {
        if(device_format)for(int x=0;x<width;x++)pixels[y*466+a->x1+x]=(uint16_t)((map[x*2]<<8)|map[x*2+1]);
        else memcpy(pixels+y*466+a->x1,map,(size_t)width*2);
        map+=width*2;
    }
    lv_display_flush_ready(d);
}
static void labels(lv_obj_t *o) {
    if (ui_obj_is_hidden(o)) return;
    if (lv_obj_check_type(o,&lv_label_class)) {
        lv_area_t a;lv_obj_get_coords(o,&a);const char *s=lv_label_get_text(o);
        uint32_t at=0;while(s[at]){uint32_t cp=lv_text_encoded_next(s,&at);lv_font_glyph_dsc_t glyph;
            if(!lv_font_get_glyph_dsc(lv_obj_get_style_text_font(o,0),&glyph,cp,0) || glyph.is_placeholder){fprintf(stderr,"missing glyph U+%04x in %s\n",cp,s);assert(false);}}
        for(int i=0;i<4;++i) {
            if (hypot((i&1?a.x2:a.x1)-232.5,(i&2?a.y2:a.y1)-232.5)>225)
                fprintf(stderr,"label outside circle: %s (%d,%d)-(%d,%d)\n",s,a.x1,a.y1,a.x2,a.y2);
            assert(hypot((i&1?a.x2:a.x1)-232.5,(i&2?a.y2:a.y1)-232.5)<=225);
        }
    }
    for(uint32_t i=0;i<lv_obj_get_child_count(o);++i) labels(lv_obj_get_child(o,i));
}
static void capture(const char *dir,const char *name) {
    lv_obj_update_layout(lv_screen_active());lv_obj_update_layout(lv_layer_top());
    labels(lv_screen_active());labels(lv_layer_top());
    lv_tick_inc(20);lv_timer_handler();lv_refr_now(display);
    if (!dir) return;
    char path[512];snprintf(path,sizeof path,"%s/capsules-%s-%s.ppm",dir,name,language?"zh":"en");
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n466 466\n255\n");
    for(int i=0;i<466*466;++i) {uint16_t p=pixels[i];uint8_t rgb[]={((p>>11)&31)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};fwrite(rgb,1,3,f);}
    assert(fclose(f)==0);
}
static void stable_page(lv_obj_t *page) {
    // 局部失效应与完整重画一致；静止状态连续五秒不产生无效刷新。
    // Partial invalidation must match a full repaint, with no redundant flushes over five stable seconds.
    lv_refr_now(display);memcpy(reference_pixels,pixels,sizeof pixels);
    lv_obj_invalidate(page);lv_refr_now(display);assert(!memcmp(reference_pixels,pixels,sizeof pixels));
    flushed_pixels=0;
    for(int i=0;i<100;i++){lv_tick_inc(50);capsules_tick();lv_refr_now(display);}
    assert(flushed_pixels==0);
}
int main(int argc,char **argv) {
    const char *dir=argc>1?argv[1]:NULL;
    device_format=getenv("SOROUND_TEST_DEVICE_FORMAT")!=NULL;
    lv_init();display=lv_display_create(466,466);lv_display_set_color_format(display,device_format?LV_COLOR_FORMAT_RGB565_SWAPPED:LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,buffer,device_format?second_buffer:NULL,device_format?466*40*2:sizeof buffer,LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
    lv_obj_set_style_bg_color(lv_screen_active(),lv_color_hex(0),0);tools_test_battery();
    lv_obj_t *title=lv_label_create(lv_layer_top()),*back=lv_obj_create(lv_layer_top()),*arrow=lv_label_create(back);
    lv_obj_set_style_radius(back,LV_RADIUS_CIRCLE,0);lv_obj_set_style_pad_all(back,0,0);
    for(language=0;language<2;++language) {
        memset(&fixture,0,sizeof fixture);fixture.state=CAPSULE_READY;fixture.result=CAPSULE_OK;fixture.catalog.free_bytes=7000000;
        lv_label_set_text(title,tools_text("CAPSULES","声音胶囊"));tools_header(title,back,arrow);lv_obj_set_style_text_font(title,&font_companion_24,0);tools_label_center(title,233,63);
        lv_obj_t *page=tools_surface(lv_screen_active(),0,0,466,466);capsules_enter(page);capture(dir,"idle");
        lv_obj_send_event(g_record,LV_EVENT_CLICKED,NULL);assert(recordings==(unsigned)language+1u);
        fixture.state=CAPSULE_RECORDING;fixture.samples=18*CAPSULE_RATE;
        for(unsigned i=0;i<32;++i)fixture.waveform[i]=(uint8_t)((i*29+51)%220);
        capsules_tick();assert(!strcmp(lv_label_get_text(g_time),"00") && !strcmp(lv_label_get_text(g_seconds),"18"));capture(dir,"recording");
        lv_obj_send_event(g_record,LV_EVENT_CLICKED,NULL);assert(stops==(unsigned)language*2u+1u);
        capsules_visibility(false);assert(stops==(unsigned)language*2u+2u);capsules_visibility(true);
        fixture.state=CAPSULE_ERROR;fixture.result=CAPSULE_UNCONFIGURED;capsules_tick();capture(dir,"unconfigured");
        unsigned before=recordings;lv_obj_send_event(g_record,LV_EVENT_CLICKED,NULL);assert(recordings==before);
        fixture.state=CAPSULE_READY;fixture.result=CAPSULE_OK;fixture.catalog.count=2;
        for(unsigned i=0;i<2;++i){capsule_entry_t *entry=&fixture.catalog.entries[i];snprintf(entry->name,sizeof entry->name,"%08X.WAV",i+1);entry->created=1792396800+i*70;entry->bytes=(18+i*10)*32000;entry->valid=true;}
        capsules_tick();show_list(true);capture(dir,"list");
        lv_obj_send_event(g_rows[0],LV_EVENT_LONG_PRESSED,NULL);assert(s_delete==0);capture(dir,"delete-confirm");
        lv_obj_send_event(g_rows[0],LV_EVENT_SHORT_CLICKED,NULL);assert(deletes==(unsigned)language+1u);
        lv_obj_send_event(g_rows[1],LV_EVENT_SHORT_CLICKED,NULL);assert(plays==(unsigned)language+1u && !s_listing);
        show_list(true);assert(capsules_back() && !s_listing);assert(!capsules_back());
        const capsule_state_t states[]={CAPSULE_READY,CAPSULE_RECORDING,CAPSULE_PLAYING,CAPSULE_ERROR};
        for(unsigned i=0;i<sizeof states/sizeof states[0];i++){
            fixture.state=states[i];fixture.result=i==3?CAPSULE_UNCONFIGURED:CAPSULE_OK;capsules_tick();stable_page(page);
        }
        fixture.state=CAPSULE_RECORDING;fixture.result=CAPSULE_OK;capsules_tick();lv_refr_now(display);flushed_pixels=0;
        fixture.samples+=CAPSULE_RATE;fixture.waveform[0]^=0x80;capsules_tick();lv_refr_now(display);
        assert(flushed_pixels>0 && !strcmp(lv_label_get_text(g_seconds),"19"));stable_page(page);
        fixture.state=CAPSULE_READY;show_list(true);stable_page(page);
        lv_obj_send_event(g_rows[0],LV_EVENT_LONG_PRESSED,NULL);stable_page(page);
        capsules_exit();assert(!g_main);lv_obj_delete(page);
    }
    assert(enters==2 && exits==2);
    puts("capsule UI: actual LVGL EN/ZH idle/recording/storage/list/delete snapshots, circular layout, record/stop/play, two-step deletion, lock visibility and back/exit passed");
}
