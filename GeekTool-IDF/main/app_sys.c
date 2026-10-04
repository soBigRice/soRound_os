// System telemetry is sampled outside drawing, using disjoint byte-addressable heaps.
#include "app.h"
#include "tools_ui.h"
#include "ui_update.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_idf_version.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

typedef struct {size_t total,free,largest,minimum;} sys_memory_t;
static sys_memory_t s_memory[2];
static uint64_t s_uptime,s_last_us;
static unsigned s_tasks,s_page;
static uint32_t s_flash;
static size_t s_psram;
static int s_cores;
static bool s_visible;
static lv_obj_t *s_body,*s_tabs[3];
LV_FONT_DECLARE(font_weather_16);
LV_FONT_DECLARE(font_cn16);
// Reuse the existing compact ASCII font with this page's CJK fallback only.
static lv_font_t s_text_font;
#define SYS_TEXT (&s_text_font)

static void text(lv_layer_t *layer,const char *value,int x,int y,int width,
                 const lv_font_t *font,uint32_t color) {
    lv_draw_label_dsc_t d;lv_draw_label_dsc_init(&d);
    d.base.obj=s_body;d.text=value;d.text_local=1;d.font=font;d.color=lv_color_hex(color);d.align=LV_TEXT_ALIGN_CENTER;
    lv_point_t sz;lv_text_get_size(&sz,value,font,0,0,width,LV_TEXT_FLAG_NONE);
    int left=x+(width-sz.x)/2;
    lv_area_t a={left,y,left+sz.x-1,y+sz.y-1};lv_draw_label(layer,&d,&a);
}
static void line_text(lv_layer_t *layer,const char *value,int y) {
    text(layer,value,69,y,328,SYS_TEXT,TOOLS_GRAY);
}
static bool available(const sys_memory_t *m) {
    return m->total && m->free<=m->total && m->largest<=m->free && m->minimum<=m->free;
}
static unsigned percent(const sys_memory_t *m) {
    return available(m)?(unsigned)(((uint64_t)(m->total-m->free)*100+m->total/2)/m->total):0;
}
static void size_text(char *out,size_t length,size_t bytes) {
    if(bytes<1024*1024)snprintf(out,length,"%.0f KiB",bytes/1024.0);
    else snprintf(out,length,"%.2f MiB",bytes/(1024.0*1024));
}
static void memory_draw(lv_layer_t *layer) {
    sys_memory_t total={.total=s_memory[0].total+s_memory[1].total,
                        .free=s_memory[0].free+s_memory[1].free};
    bool valid=available(&s_memory[0]) && ((!s_memory[1].total && !s_memory[1].free) || available(&s_memory[1]));
    unsigned used=percent(&total);
    line_text(layer,tools_text("MEMORY USAGE","内存使用"),105);
    for(int i=0;i<48;++i) {
        float angle=(135+i*270.0f/47)*(3.14159265f/180);
        bool lit=valid && (unsigned)i*100<used*48;
        tools_dot(layer,233+(int)lroundf(cosf(angle)*91),218+(int)lroundf(sinf(angle)*91),
                  i%4?4:6,lit?(used>=90?COL_RED:TOOLS_WHITE):TOOLS_LINE);
    }
    char buf[80],a[24],b[24];
    if(valid) {
        snprintf(buf,sizeof buf,"%u",used);
        lv_point_t sz;lv_text_get_size(&sz,buf,&font_tools_60,0,0,328,LV_TEXT_FLAG_NONE);
        int x=233-(sz.x+24)/2;
        text(layer,buf,x,176,sz.x,&font_tools_60,TOOLS_WHITE);
        text(layer,"%",x+sz.x+8,207,16,SYS_TEXT,TOOLS_GRAY);
    } else text(layer,"--",173,196,120,UI_FONT_SYM,TOOLS_GRAY);
    line_text(layer,tools_text("USED","已用"),252);
    size_text(a,sizeof a,total.total);
    snprintf(buf,sizeof buf,"%s  %s",tools_text("Heap total","堆内存总量"),valid?a:"--");
    line_text(layer,buf,304);
    for(int i=0;i<2;++i) {
        int x=i?254:74;
        text(layer,i?tools_text("FREE","可用"):tools_text("USED","已用"),x,341,138,SYS_TEXT,TOOLS_GRAY);
        size_text(b,sizeof b,valid?(i?total.free:total.total-total.free):0);
        text(layer,valid?b:"--",x,363,138,UI_FONT_SYM,i?TOOLS_WHITE:TOOLS_GRAY);
    }
}
static void details_draw(lv_layer_t *layer) {
    char buf[96],a[24],b[24];
    for(int i=0;i<2;++i) {
        const sys_memory_t *m=&s_memory[i];int y=i?255:104;bool valid=available(m);
        text(layer,i?"PSRAM":tools_text("INTERNAL RAM","内部 RAM"),69,y,328,UI_FONT_SYM,TOOLS_WHITE);
        size_text(a,sizeof a,valid?m->total-m->free:0);size_text(b,sizeof b,m->total);
        snprintf(buf,sizeof buf,"%s  %s / %s",tools_text("Used","已用"),valid?a:"--",valid?b:"--");
        line_text(layer,buf,y+35);
        tools_line(layer,107,y+63,359,y+63,7,TOOLS_DIM);
        unsigned used=percent(m);
        if(valid && used)tools_line(layer,107,y+63,107+(int)(252*used/100),y+63,7,used>=90?COL_RED:TOOLS_WHITE);
        size_text(a,sizeof a,m->free);size_text(b,sizeof b,m->largest);
        snprintf(buf,sizeof buf,"%s %s   %s %s",tools_text("Free","可用"),valid?a:"--",
                 tools_text("Largest","最大连续"),valid?b:"--");line_text(layer,buf,y+85);
        size_text(a,sizeof a,m->minimum);
        snprintf(buf,sizeof buf,"%s  %s",tools_text("Lowest free","历史最低可用"),valid?a:"--");line_text(layer,buf,y+109);
    }
    tools_line(layer,145,238,321,238,1,TOOLS_DIM);
}
static void device_draw(lv_layer_t *layer) {
    char buf[96],a[24];
    text(layer,CONFIG_IDF_TARGET,69,108,328,UI_FONT_SYM,TOOLS_WHITE);
    snprintf(buf,sizeof buf,"%d %s / %d MHz",s_cores,tools_text("cores","核心"),CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    line_text(layer,buf,140);tools_line(layer,145,174,321,174,1,TOOLS_DIM);
    size_text(a,sizeof a,s_flash);snprintf(buf,sizeof buf,"Flash   %s",s_flash?a:"--");line_text(layer,buf,196);
    size_text(a,sizeof a,s_psram);snprintf(buf,sizeof buf,"PSRAM   %s",s_psram?a:"--");line_text(layer,buf,226);
    tools_line(layer,145,264,321,264,1,TOOLS_DIM);
    snprintf(buf,sizeof buf,"%s  %.31s",tools_text("Firmware","固件"),esp_app_get_description()->version);line_text(layer,buf,285);
    snprintf(buf,sizeof buf,"SDK  %.36s",esp_get_idf_version());line_text(layer,buf,315);
    snprintf(buf,sizeof buf,"%s  %llu:%02u:%02u",tools_text("Uptime","运行时长"),
        (unsigned long long)(s_uptime/3600),(unsigned)(s_uptime%3600/60),(unsigned)(s_uptime%60));line_text(layer,buf,345);
    snprintf(buf,sizeof buf,"%s  %u",tools_text("Tasks","任务数"),s_tasks);line_text(layer,buf,375);
}
static void draw(lv_event_t *e) {
    lv_layer_t *layer=lv_event_get_layer(e);
    if(s_page==0)memory_draw(layer);else if(s_page==1)details_draw(layer);else device_draw(layer);
}
static void sys_tick(void) {
    if(!s_body || !s_visible)return;
    uint64_t now=(uint64_t)esp_timer_get_time();
    if(s_last_us && now-s_last_us<1000000)return;
    s_last_us=now;
    sys_memory_t next[2]={0};
    for(int i=0;i<2;++i) {
        uint32_t caps=MALLOC_CAP_8BIT|(i?MALLOC_CAP_SPIRAM:MALLOC_CAP_INTERNAL);
        multi_heap_info_t info;heap_caps_get_info(&info,caps);
        next[i]=(sys_memory_t){heap_caps_get_total_size(caps),info.total_free_bytes,
                              info.largest_free_block,info.minimum_free_bytes};
    }
    uint64_t uptime=now/1000000;unsigned tasks=(unsigned)uxTaskGetNumberOfTasks();
    bool changed=s_page==2?(uptime!=s_uptime || tasks!=s_tasks):memcmp(next,s_memory,sizeof next)!=0;
    memcpy(s_memory,next,sizeof next);s_uptime=uptime;s_tasks=tasks;
    if(changed)lv_obj_invalidate(s_body);
}
static void select_page(unsigned page) {
    s_page=page;
    for(unsigned i=0;i<3;++i) {
        ui_bg_opa(s_tabs[i],i==page?LV_OPA_COVER:LV_OPA_TRANSP);
        lv_obj_set_style_text_color(s_tabs[i],lv_color_hex(i==page?TOOLS_WHITE:TOOLS_GRAY),0);
    }
    s_last_us=0;sys_tick();lv_obj_invalidate(s_body);
}
static void tab(lv_event_t *e) {select_page((unsigned)(uintptr_t)lv_event_get_user_data(e));}
static void sys_enter(lv_obj_t *parent) {
    s_text_font=font_weather_16;s_text_font.fallback=&font_cn16;
    s_body=tools_surface(parent,0,0,466,466);lv_obj_add_event_cb(s_body,draw,LV_EVENT_DRAW_MAIN,NULL);
    s_visible=true;s_last_us=0;s_uptime=0;memset(s_memory,0,sizeof s_memory);
    esp_chip_info_t chip;esp_chip_info(&chip);s_cores=chip.cores;
    s_flash=0;if(esp_flash_get_size(NULL,&s_flash)!=ESP_OK)s_flash=0;
    s_psram=esp_psram_get_size();
    for(unsigned i=0;i<3;++i) {
        s_tabs[i]=lv_button_create(parent);lv_obj_remove_style_all(s_tabs[i]);
        lv_obj_set_pos(s_tabs[i],129+(int)i*72,402);lv_obj_set_size(s_tabs[i],64,30);
        lv_obj_set_style_bg_color(s_tabs[i],lv_color_hex(TOOLS_DIM),0);lv_obj_set_style_radius(s_tabs[i],15,0);
        lv_obj_t *label=lv_label_create(s_tabs[i]);lv_obj_set_style_text_font(label,SYS_TEXT,0);
        lv_label_set_text(label,i==0?tools_text("Usage","总览"):i==1?tools_text("Memory","内存"):tools_text("Device","设备"));
        lv_obj_center(label);lv_obj_add_event_cb(s_tabs[i],tab,LV_EVENT_CLICKED,(void *)(uintptr_t)i);
    }
    select_page(0);
}
static void sys_visibility(bool visible) {s_visible=visible;if(visible){s_last_us=0;sys_tick();}}
static void sys_exit(void) {s_body=NULL;memset(s_tabs,0,sizeof s_tabs);s_visible=false;}
const app_t app_sys={"System",COL_SYS,sys_enter,sys_tick,sys_exit,NULL,1000,sys_visibility};
