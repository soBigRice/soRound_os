// Shared weather cache. HTTP task never touches LVGL/NVS; location generations
// prevent a previous city's late response from appearing under the new title.
#include "app.h"
#include "settings.h"
#include "weather_ui.h"
#include "weather_locations.h"
#include "weather_location_ui.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

static const char *TAG="weather";
#define WX_BUF 8192
// Public forecast, retaining existing HTTP transport and memory budget.
#define WX_URL "http://api.open-meteo.com/v1/forecast?latitude=%.5f&longitude=%.5f&current=temperature_2m,relative_humidity_2m,weather_code,is_day&daily=temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=1"
typedef enum {WX_IDLE,WX_LOADING,WX_OK,WX_FAIL} wx_state_t;
static portMUX_TYPE s_lock=portMUX_INITIALIZER_UNLOCKED;
static wx_state_t s_state=WX_IDLE,s_shown=(wx_state_t)-1;
static bool s_task_alive;
static uint32_t s_revision,s_shown_revision,s_generation,s_last_fetch;
static int s_temp_i,s_lo,s_hi,s_hum_i,s_code=-1;
static bool s_is_day=true;
static weather_ui_t s_ui;
static lv_obj_t *s_parent,*s_content;
static bool s_choosing;
static struct {uint32_t generation;int32_t lat,lon;} s_request;

static float json_num(const char *body,const char *key,float fallback) {
    if(!body) return fallback;
    char pat[48];snprintf(pat,sizeof pat,"\"%s\"",key);
    const char *p=strstr(body,pat);if(!p) return fallback;p+=strlen(pat);
    while(isspace((unsigned char)*p))++p;
    if(*p++!=':')return fallback;
    while(isspace((unsigned char)*p) || *p=='[')++p;
    char *end;float value=strtof(p,&end);
    return end==p || !isfinite(value)?fallback:value;
}
static void wx_task(void *arg) {
    (void)arg;
    portENTER_CRITICAL(&s_lock);
    uint32_t generation=s_request.generation;int32_t lat=s_request.lat,lon=s_request.lon;
    portEXIT_CRITICAL(&s_lock);
    char url[400];snprintf(url,sizeof url,WX_URL,lat/100000.0,lon/100000.0);
    esp_http_client_config_t cfg={.url=url,.crt_bundle_attach=esp_crt_bundle_attach,.timeout_ms=12000};
    esp_http_client_handle_t cli=esp_http_client_init(&cfg);
    char *body=heap_caps_malloc(WX_BUF,MALLOC_CAP_SPIRAM);if(!body)body=malloc(WX_BUF);
    int total=0,status=0;bool opened=false;
    if(cli && body && esp_http_client_open(cli,0)==ESP_OK) {
        opened=true;esp_http_client_fetch_headers(cli);status=esp_http_client_get_status_code(cli);
        int nb;
        while((nb=esp_http_client_read(cli,body+total,WX_BUF-1-total))>0) {
            total+=nb;if(total>=WX_BUF-1)break;
        }
        body[total]=0;
    }
    if(cli) {if(opened)esp_http_client_close(cli);esp_http_client_cleanup(cli);}
    bool valid=false;int temp=0,lo=0,hi=0,hum=0,code=-1;bool is_day=true;
    if(total>0 && total<WX_BUF-1 && status==200) {
        const char *current=strstr(body,"\"current\":");const char *daily=strstr(body,"\"daily\":");
        float t=json_num(current,"temperature_2m",NAN),h=json_num(current,"relative_humidity_2m",NAN);
        float c=json_num(current,"weather_code",NAN),d=json_num(current,"is_day",NAN);
        float low=json_num(daily,"temperature_2m_min",NAN),high=json_num(daily,"temperature_2m_max",NAN);
        valid=isfinite(t)&&t>=-100&&t<=100&&isfinite(h)&&h>=0&&h<=100&&isfinite(c)&&c>=0&&c<=100&&floorf(c)==c
              &&(d==0 || d==1)&&isfinite(low)&&isfinite(high)&&low>=-100&&high<=100&&low<=high;
        if(valid) {temp=(int)lroundf(t);hum=(int)lroundf(h);code=(int)c;lo=(int)lroundf(low);hi=(int)lroundf(high);is_day=d!=0;}
    }
    free(body);
    portENTER_CRITICAL(&s_lock);
    if(generation==s_generation) {
        if(valid) {s_temp_i=temp;s_lo=lo;s_hi=hi;s_hum_i=hum;s_code=code;s_is_day=is_day;}
        s_state=valid?WX_OK:WX_FAIL;++s_revision;
    }
    s_task_alive=false;
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG,"forecast generation=%lu %s",(unsigned long)generation,valid?"ready":"failed");
    vTaskDelete(NULL);
}
static void start_fetch(void) {
    uint16_t selected=wx_location_selected();
    portENTER_CRITICAL(&s_lock);bool alive=s_task_alive;portEXIT_CRITICAL(&s_lock);if(alive)return;
    wifi_ap_record_t ap;bool online=esp_wifi_sta_get_ap_info(&ap)==ESP_OK;
    portENTER_CRITICAL(&s_lock);
    if(s_task_alive) {portEXIT_CRITICAL(&s_lock);return;}
    s_state=online?WX_LOADING:WX_FAIL;++s_revision;
    s_last_fetch=(uint32_t)(esp_timer_get_time()/1000);
    if(online) {
        s_task_alive=true;s_request.generation=s_generation;
        s_request.lat=wx_locations[selected].lat_e5;s_request.lon=wx_locations[selected].lon_e5;
    }
    portEXIT_CRITICAL(&s_lock);
    if(online && xTaskCreate(wx_task,"wx",8192,NULL,5,NULL)!=pdPASS) {
        portENTER_CRITICAL(&s_lock);s_task_alive=false;s_state=WX_FAIL;++s_revision;portEXIT_CRITICAL(&s_lock);
    }
}
void weather_poll(void) {
    wx_location_init();uint32_t now=(uint32_t)(esp_timer_get_time()/1000);
    portENTER_CRITICAL(&s_lock);
    uint32_t period=s_state==WX_OK?20u*60*1000:60u*1000;
    bool due=!s_task_alive && (s_state==WX_IDLE || !s_last_fetch || now-s_last_fetch>=period);
    portEXIT_CRITICAL(&s_lock);if(due)start_fetch();
}
bool weather_cached(int *temp,int *lo,int *hi,int *code,int *hum) {
    portENTER_CRITICAL(&s_lock);bool ok=s_state==WX_OK;
    if(ok) {if(temp)*temp=s_temp_i;if(lo)*lo=s_lo;if(hi)*hi=s_hi;if(code)*code=s_code;if(hum)*hum=s_hum_i;}
    portEXIT_CRITICAL(&s_lock);return ok;
}
static void city_title(void) {launcher_set_title(wx_location_name(wx_location_selected(),settings_lang()));}
static bool select_location(uint16_t index) {
    if(!wx_location_select(index))return false;
    portENTER_CRITICAL(&s_lock);++s_generation;++s_revision;s_state=WX_IDLE;s_last_fetch=0;portEXIT_CRITICAL(&s_lock);
    s_ui.has_data=false;lv_obj_invalidate(s_ui.icon);lv_obj_invalidate(s_ui.temperature);
    s_choosing=false;lv_obj_remove_flag(s_content,LV_OBJ_FLAG_HIDDEN);city_title();start_fetch();return true;
}
static void choose_location(lv_event_t *e) {
    (void)e;s_choosing=true;lv_obj_add_flag(s_content,LV_OBJ_FLAG_HIDDEN);weather_location_ui_open(s_parent,select_location);
}
static void retry(lv_event_t *e) {(void)e;start_fetch();}
static void weather_enter(lv_obj_t *parent) {
    s_parent=parent;city_title();s_shown=(wx_state_t)-1;s_shown_revision=UINT32_MAX;s_choosing=false;
    s_content=lv_obj_create(parent);lv_obj_remove_style_all(s_content);lv_obj_set_size(s_content,466,466);
    lv_obj_remove_flag(s_content,LV_OBJ_FLAG_SCROLLABLE);weather_ui_create(&s_ui,s_content);
    lv_obj_t *city=lv_button_create(s_content);lv_obj_remove_style_all(city);lv_obj_set_size(city,190,48);
    lv_obj_align(city,LV_ALIGN_TOP_MID,25,47);lv_obj_add_event_cb(city,choose_location,LV_EVENT_CLICKED,NULL);
    // The header label lives on the top layer; its non-clickable text allows this
    // generous hit area beneath it to receive taps on city/pin.
    lv_obj_add_flag(s_ui.status,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(s_ui.status,retry,LV_EVENT_CLICKED,NULL);
    start_fetch();
}
static void weather_tick(void) {
    weather_poll();
    portENTER_CRITICAL(&s_lock);
    wx_state_t state=s_state;uint32_t revision=s_revision;
    int t=s_temp_i,lo=s_lo,hi=s_hi,code=s_code,hum=s_hum_i;bool day=s_is_day;
    portEXIT_CRITICAL(&s_lock);
    if(!s_ui.status || (state==s_shown && revision==s_shown_revision))return;
    s_shown=state;s_shown_revision=revision;
    if(state==WX_OK)weather_ui_show(&s_ui,t,lo,hi,code,hum,day);
    else weather_ui_status(&s_ui,state==WX_LOADING || state==WX_IDLE);
}
static bool weather_back(void) {
    if(!s_choosing)return false;
    weather_location_ui_back();
    if(!weather_location_ui_visible()) {
        s_choosing=false;lv_obj_remove_flag(s_content,LV_OBJ_FLAG_HIDDEN);city_title();
    }
    return true;
}
static void weather_exit(void) {
    weather_location_ui_close();memset(&s_ui,0,sizeof s_ui);s_parent=s_content=NULL;s_choosing=false;
}
const app_t app_weather={"weather",COL_TXT,weather_enter,weather_tick,weather_exit,weather_back};
