// Real LVGL renderer and Goertzel worker; only task scheduling / microphone hardware are substituted.
#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "tools_ui.h"
#include "audio_sdk.h"
#include "audio_mic.h"
#include "audio_bus.h"
#include "board_config.h"
#include "../host/tools_render.h"
#include "src/misc/lv_text_private.h"

static uint8_t language;
uint8_t settings_lang(void) {return language;}
static bool allocation_ok=true,mic_ok=true;
static int task_creates,reads,stops,releases,notifications;
static jmp_buf worker_yield;
static bool pumping;
int xTaskCreate(void (*task)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle) {
    (void)task;(void)name;(void)stack;(void)arg;(void)priority;++task_creates;
    if (!allocation_ok) return 0;
    *(void **)handle=(void *)1;return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t handle) {assert(handle);++notifications;}
unsigned ulTaskNotifyTake(int clear,unsigned timeout) {(void)clear;(void)timeout;assert(pumping);longjmp(worker_yield,1);}
void vTaskDelay(int delay) {(void)delay;}
void vTaskDelete(void *task) {(void)task;assert(pumping);longjmp(worker_yield,1);}
i2c_master_bus_handle_t board_i2c_bus(void) {return (void *)1;}
bool audio_bus_acquire(TickType_t wait) {(void)wait;return true;}
void audio_bus_release(void) {++releases;}
bool audio_mic_start(i2c_master_bus_handle_t bus) {assert(bus);return mic_ok;}
void audio_mic_stop(void) {++stops;}
int audio_mic_read(int16_t *samples,int count) {
    if (++reads>1) longjmp(worker_yield,1);
    // A real 1kHz waveform exercises the existing analysis, not injected band heights.
    for (int i=0;i<count;++i) samples[i]=(int16_t)(6000*sinf(i*6.2831853f*1000/16000));
    return count;
}
#include "../../main/app_audio.c"

static uint16_t buffer[466*466],pixels[466*466];
static lv_display_t *display;
static unsigned flushed_pixels;
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *map) {
    int width=lv_area_get_width(a);
    flushed_pixels+=(unsigned)(width*lv_area_get_height(a));
    for (int y=a->y1;y<=a->y2;++y) {memcpy(pixels+y*466+a->x1,map,(size_t)width*2);map+=width*2;}
    lv_display_flush_ready(d);
}
static void labels(lv_obj_t *o) {
    if (ui_obj_is_hidden(o)) return;
    if (lv_obj_check_type(o,&lv_label_class)) {
        lv_area_t a;lv_obj_get_coords(o,&a);const char *s=lv_label_get_text(o);
        uint32_t at=0;while(s[at]) {uint32_t cp=lv_text_encoded_next(s,&at);lv_font_glyph_dsc_t glyph;
            assert(lv_font_get_glyph_dsc(lv_obj_get_style_text_font(o,0),&glyph,cp,0) && !glyph.is_placeholder);}
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
    char path[512];snprintf(path,sizeof path,"%s/audio-%s-%s.ppm",dir,name,language?"zh":"en");
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n466 466\n255\n");
    for(int i=0;i<466*466;++i) {uint16_t p=pixels[i];uint8_t rgb[]={((p>>11)&31)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};fwrite(rgb,1,3,f);}
    assert(fclose(f)==0);
}
static void pump(void) {pumping=true;if (!setjmp(worker_yield)) audio_task(NULL);pumping=false;}
int main(int argc,char **argv) {
    const char *dir=argc>1?argv[1]:NULL;
    lv_init();display=lv_display_create(466,466);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,buffer,NULL,sizeof buffer,LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
    lv_obj_set_style_bg_color(lv_screen_active(),lv_color_hex(0),0);
    tools_test_battery();
    lv_obj_t *title=lv_label_create(lv_layer_top()),*back=lv_obj_create(lv_layer_top()),*arrow=lv_label_create(back);
    lv_obj_set_style_radius(back,LV_RADIUS_CIRCLE,0);lv_obj_set_style_pad_all(back,0,0);
    for(language=0;language<2;++language) {
        lv_label_set_text(title,tools_text("AUDIO","音频"));tools_header(title,back,arrow);
        lv_obj_t *page=tools_surface(lv_screen_active(),0,0,466,466);
        s_worker=NULL;allocation_ok=true;audio_enter(page);capture(dir,"starting");
        s_capture=CAPTURE_READY;memset(s_band,0,sizeof s_band);audio_tick();capture(dir,"quiet");
        for(int b=0;b<NB;++b) assert(s_colors[b][0]==TOOLS_WHITE && s_colors[b][1]==TOOLS_FAINT);
        const float voice[]={.21,.28,.39,.52,.65,.82,.69,.49,.61,.75,.59,.43,.34,.28,.19,.15,.13,.08};
        memcpy(s_band,voice,sizeof voice);audio_tick();capture(dir,"voice");
        for(int b=0;b<NB;++b) {int n=(int)roundf(voice[b]*16);if(n<1)n=1;assert(s_colors[b][n-1]==COL_RED);}
        for(int b=0;b<NB;++b)s_band[b]=.95f;audio_tick();capture(dir,"strong");
        assert(s_colors[0][14]!=s_colors[0][0] && s_colors[0][14]!=TOOLS_FAINT);
        flushed_pixels=0;audio_tick();lv_refr_now(display);assert(flushed_pixels==0);
        s_band[7]=.70f;audio_tick();lv_refr_now(display);assert(flushed_pixels>0 && flushed_pixels<5000);
        uint32_t old=s_generation;capture_publish(old-1,CAPTURE_FAILED,NULL);assert(s_capture==CAPTURE_READY);
        capture_publish(old,CAPTURE_FAILED,NULL);audio_tick();assert(ui_obj_is_hidden(g_content));capture(dir,"fault");
        capture_publish(old,CAPTURE_READY,voice);audio_tick();assert(ui_obj_is_hidden(g_fault));
        int before=task_creates;audio_exit();audio_enter(page);assert(task_creates==before); // Reuse the still-alive worker.
        audio_visibility(false);assert(!s_visible);audio_visibility(true);assert(s_visible);
        audio_exit();lv_obj_delete(page);
    }
    lv_obj_t *page=tools_surface(lv_screen_active(),0,0,466,466);
    s_worker=NULL;mic_ok=true;reads=0;audio_enter(page);pump();assert(s_capture==CAPTURE_READY);
    float max=0;int strongest=-1;for(int b=0;b<NB;++b)if(s_band[b]>max){max=s_band[b];strongest=b;}
    float hz=80*powf(6000.0f/80,strongest/17.0f);assert(max>.1f && hz>700 && hz<1300);
    audio_exit();pump();assert(!s_worker);lv_obj_delete(page);
    page=tools_surface(lv_screen_active(),0,0,466,466);mic_ok=false;audio_enter(page);pump();
    assert(s_capture==CAPTURE_FAILED && stops>0 && releases>0);audio_exit();pump();lv_obj_delete(page);
    page=tools_surface(lv_screen_active(),0,0,466,466);allocation_ok=false;audio_enter(page);
    assert(s_capture==CAPTURE_FAILED && !s_run);audio_exit();lv_obj_delete(page);
    assert(notifications>0);puts("audio: actual spectrum analysis, energy colors, quiet/fault/recovery, activation guard, reuse/visibility, EN/ZH glyphs/round layout, idle no-redraw and partial updates passed");
}
