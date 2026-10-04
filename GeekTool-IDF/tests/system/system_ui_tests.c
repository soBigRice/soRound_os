// Native system UI and telemetry math; only ESP-IDF services are substituted.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "settings.h"
#include "sdk.h"
#include "esp_heap_caps.h"
#include "esp_chip_info.h"
#include "src/misc/lv_text_private.h"
#include "../host/tools_render.h"

static uint8_t language;
static uint64_t clock_us=90061ULL*1000000;
static unsigned task_count=17,reads;
static bool flash_ok=true;
static size_t physical_psram=8*1024*1024;
static size_t totals[2]={336*1024,8*1024*1024};
static multi_heap_info_t heaps[2]={{.total_free_bytes=192*1024,.largest_free_block=160*1024,.minimum_free_bytes=128*1024},
    {.total_free_bytes=5505024,.largest_free_block=5*1024*1024,.minimum_free_bytes=4718592}};
static esp_app_desc_t descriptor={.version="v1.7-beta.16"};
uint8_t settings_lang(void) {return language;}
int64_t esp_timer_get_time(void) {return (int64_t)clock_us;}
unsigned uxTaskGetNumberOfTasks(void) {return task_count;}
void esp_chip_info(esp_chip_info_t *chip) {chip->cores=2;}
esp_err_t esp_flash_get_size(void *chip,uint32_t *size) {(void)chip;*size=32*1024*1024;return flash_ok?ESP_OK:ESP_FAIL;}
size_t esp_psram_get_size(void) {return physical_psram;}
const char *esp_get_idf_version(void) {return "v6.0.1";}
const esp_app_desc_t *esp_app_get_description(void) {return &descriptor;}
static unsigned pool(uint32_t caps) {
    assert(caps==(MALLOC_CAP_8BIT|MALLOC_CAP_INTERNAL) || caps==(MALLOC_CAP_8BIT|MALLOC_CAP_SPIRAM));
    return (caps&MALLOC_CAP_SPIRAM)?1:0;
}
void heap_caps_get_info(multi_heap_info_t *info,uint32_t caps) {*info=heaps[pool(caps)];++reads;}
size_t heap_caps_get_total_size(uint32_t caps) {return totals[pool(caps)];}
#include "../../main/app_sys.c"

#define W 466
static lv_display_t *display;
static uint16_t buffer[W*W],pixels[W*W];
static uint64_t flushed;
static char drawn[48][96];
static lv_area_t labels[48];
static unsigned label_count,renders;
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *map) {
    int width=lv_area_get_width(a);flushed+=(uint64_t)width*lv_area_get_height(a);
    for(int y=a->y1;y<=a->y2;++y) {memcpy(pixels+y*W+a->x1,map,(size_t)width*2);map+=width*2;}
    lv_display_flush_ready(d);
}
static void audit(lv_event_t *e) {
    lv_draw_task_t *task=lv_event_get_draw_task(e);lv_draw_label_dsc_t *d=lv_draw_task_get_label_dsc(task);if(!d)return;
    uint32_t pos=0;
    while(d->text[pos]) {
        uint32_t cp=lv_text_encoded_next(d->text,&pos);lv_font_glyph_dsc_t glyph;
        if(!lv_font_get_glyph_dsc(d->font,&glyph,cp,0)||glyph.is_placeholder) {
            fprintf(stderr,"missing system glyph U+%04x: %s\n",cp,d->text);assert(false);
        }
    }
    lv_area_t a;lv_draw_task_get_area(task,&a);
    for(int k=0;k<4;++k) {
        int x=k&1?a.x2:a.x1,y=k&2?a.y2:a.y1;
        if(hypot(x-232.5,y-232.5)>225) {fprintf(stderr,"system text outside circle: %s (%d,%d)\n",d->text,x,y);assert(false);}
    }
    if(lv_event_get_target_obj(e)!=s_body)return;
    assert(label_count<48);
    for(unsigned i=0;i<label_count;++i) {
        if(a.x1<=labels[i].x2 && a.x2>=labels[i].x1 && a.y1<=labels[i].y2 && a.y2>=labels[i].y1) {
            fprintf(stderr,"overlapping system labels: %s / %s\n",drawn[i],d->text);assert(false);
        }
    }
    labels[label_count]=a;snprintf(drawn[label_count++],96,"%s",d->text);
}
static void audit_tree(lv_obj_t *o) {
    ui_obj_set_send_draw_task_events(o,true);lv_obj_add_event_cb(o,audit,LV_EVENT_DRAW_TASK_ADDED,NULL);
    for(uint32_t i=0;i<lv_obj_get_child_count(o);++i)audit_tree(lv_obj_get_child(o,i));
}
static bool contains(const char *value) {
    for(unsigned i=0;i<label_count;++i)if(strstr(drawn[i],value))return true;
    fprintf(stderr,"system text not drawn: %s\n",value);
    for(unsigned i=0;i<label_count;++i)fprintf(stderr,"  %s\n",drawn[i]);
    return false;
}
static void capture(const char *dir,const char *name) {
    label_count=0;lv_obj_invalidate(s_body);lv_tick_inc(20);lv_timer_handler();lv_refr_now(display);++renders;
    if(!label_count)fprintf(stderr,"no native system draw tasks: %s page=%u language=%u render=%u\n",name,s_page,language,renders);
    assert(label_count>0);
    if(!dir)return;
    char file[512];snprintf(file,sizeof file,"%s/%s-%s.ppm",dir,name,language?"zh":"en");
    FILE *f=fopen(file,"wb");assert(f);fprintf(f,"P6\n466 466\n255\n");
    for(int i=0;i<W*W;++i) {uint16_t p=pixels[i];uint8_t rgb[]={((p>>11)&31)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};fwrite(rgb,1,3,f);}
    assert(fclose(f)==0);
}
static lv_obj_t *enter(void) {
    lv_obj_t *page=tools_surface(lv_screen_active(),0,0,W,W);sys_enter(page);audit_tree(page);return page;
}
static void next(unsigned page) {
    lv_obj_send_event(s_tabs[page],LV_EVENT_CLICKED,NULL);assert(s_page==page);
}
static void advance(void) {clock_us+=1000000;lv_tick_inc(1000);sys_tick();}
int main(int argc,char **argv) {
    const char *dir=argc>1?argv[1]:NULL;
    lv_init();i18n_init();display=lv_display_create(W,W);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,buffer,NULL,sizeof buffer,LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
    lv_obj_set_style_bg_color(lv_screen_active(),lv_color_hex(0),0);tools_test_battery();
    lv_obj_t *title=tools_label(lv_layer_top(),"System",UI_FONT_L,233,54,TOOLS_WHITE,0);
    for(language=0;language<2;++language) {
        clock_us=90061ULL*1000000;
        lv_label_set_text(title,language?"系统":"System");tools_label_center(title,233,54);
        lv_obj_t *page=enter();capture(dir,"overview");assert(contains("35") && contains("8.33 MiB"));
        assert(s_memory[0].free==192*1024 && s_memory[1].free==5505024);
        assert(contains("2.89 MiB") && contains("5.44 MiB"));
        next(1);capture(dir,"memory");assert(contains("144 KiB / 336 KiB"));
        assert(contains("160 KiB") && contains("128 KiB") && contains("5.00 MiB"));
        next(2);capture(dir,"device");assert(contains("25:01:01") && contains("v1.7-beta.16") && contains("32.00 MiB"));
        strcpy(descriptor.version,"v1.7-beta.16-123-gac697c5-dirty");
        clock_us=5000ULL*3600*1000000;advance();capture(dir,"long-running");assert(contains("5000:00:01"));
        strcpy(descriptor.version,"v1.7-beta.16");clock_us=90061ULL*1000000;
        next(0);capture(NULL,"before-idle");flushed=0;unsigned before=reads;
        advance();lv_timer_handler();lv_refr_now(display);assert(flushed==0 && reads==before+2);
        sys_tick();assert(reads==before+2);
        sys_visibility(false);before=reads;heaps[1].total_free_bytes-=1024;clock_us+=3000000;sys_tick();assert(reads==before);
        sys_visibility(true);assert(reads==before+2 && s_memory[1].free==heaps[1].total_free_bytes);
        heaps[1].total_free_bytes+=1024;advance();
        // Extreme occupancy and missing/broken providers must not divide by zero or fabricate values.
        multi_heap_info_t saved[2];memcpy(saved,heaps,sizeof saved);
        heaps[0]=(multi_heap_info_t){0};heaps[1]=(multi_heap_info_t){0};advance();capture(dir,"full");assert(contains("100"));
        heaps[0]=(multi_heap_info_t){.total_free_bytes=totals[0],.largest_free_block=totals[0],.minimum_free_bytes=totals[0]};
        heaps[1]=(multi_heap_info_t){.total_free_bytes=totals[1],.largest_free_block=totals[1],.minimum_free_bytes=totals[1]};
        advance();capture(dir,"empty");assert(contains("0") && contains("0 KiB"));
        heaps[0].total_free_bytes=totals[0]+1;advance();capture(dir,"invalid");assert(contains("--"));
        memcpy(heaps,saved,sizeof heaps);advance();sys_exit();lv_obj_delete(page);sys_tick();assert(!s_body);
        size_t psram_total=totals[1];multi_heap_info_t psram=heaps[1];
        totals[1]=0;heaps[1]=(multi_heap_info_t){0};physical_psram=0;flash_ok=false;
        page=enter();capture(dir,"internal-only");assert(contains("43"));next(1);capture(dir,"psram-missing");assert(contains("--"));
        next(2);capture(dir,"flash-missing");assert(contains("Flash   --") && contains("PSRAM   --"));
        sys_exit();lv_obj_delete(page);totals[1]=psram_total;heaps[1]=psram;physical_psram=8*1024*1024;flash_ok=true;
    }
    for(int i=0;i<20;++i) {lv_obj_t *page=enter();next((unsigned)i%3);sys_exit();lv_obj_delete(page);sys_tick();}
    assert(lv_obj_get_child_count(lv_screen_active())==0);
    printf("%u native system renders: disjoint RAM/PSRAM totals, used/free/minimum/largest, 0/100%%/missing/error, long uptime, 1Hz/pause/idle, tabs/exit, EN/ZH glyphs and round bounds passed\n",renders);
}
