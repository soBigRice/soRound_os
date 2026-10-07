// Fifteen faces: TYPE / ORBIT / SHIFT, each with dots / bold / rings / weather / image.
// NVS indices 0..4 retain the original kind mapping. BOOT and power policy belong to lock.c.
#include "watchface.h"
#include "watchface_ui.h"
#include "quickpanel.h"
#include "power.h"
#include "settings.h"
#include "img_store.h"
#include "app.h"
#include "ui_update.h"
#include "lvgl_compat.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static lv_obj_t *wf_screen,*wf_content;
static lv_timer_t *wf_timer;
static int s_idx;
static bool s_aod,s_sleep;
static time_t s_last_minute=(time_t)-1;
static watchface_data_t s_data;
static const char *const themes[]={"TYPE","ORBIT","SHIFT"};
static const char *const kinds[]={"dots","bold","rings","weather","image"};
static const char *const names[]={
    "TYPE / dots","TYPE / bold","TYPE / rings","TYPE / weather","TYPE / image",
    "ORBIT / dots","ORBIT / bold","ORBIT / rings","ORBIT / weather","ORBIT / image",
    "SHIFT / dots","SHIFT / bold","SHIFT / rings","SHIFT / weather","SHIFT / image"
};
int watchface_count(void){return WATCHFACE_COUNT;}
int watchface_selected(void){return s_idx;}
const char *watchface_name(int i){return i>=0&&i<WATCHFACE_COUNT?names[i]:"";}
const char *watchface_theme_name(int theme){return theme>=0&&theme<WATCHFACE_THEME_COUNT?themes[theme]:"";}
const char *watchface_kind_name(int index){return index>=0&&index<WATCHFACE_COUNT?kinds[index%WATCHFACE_KIND_COUNT]:"";}

static bool snapshot(bool force) {
    time_t now=time(NULL);struct tm t;localtime_r(&now,&t);
    time_t minute=now/60;bool minute_changed=minute!=s_last_minute;
    bool dirty=force||minute_changed||(!s_aod&&s_idx%5==0&&t.tm_sec!=s_data.time.tm_sec);
    s_data.time=t;s_data.aod=s_aod;
    if(force||minute_changed) {
        wifi_ap_record_t ap;s_data.wifi=wifi_service_ready() && esp_wifi_sta_get_ap_info(&ap)==ESP_OK;
        s_data.ssid[0]=s_data.ip[0]=0;
        if(s_data.wifi) {
            memcpy(s_data.ssid,ap.ssid,32);s_data.ssid[32]=0;
            esp_netif_t *n=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");esp_netif_ip_info_t ip;
            if(n&&esp_netif_get_ip_info(n,&ip)==ESP_OK&&ip.ip.addr)
                snprintf(s_data.ip,sizeof s_data.ip,IPSTR,IP2STR(&ip.ip));
        }
        int battery=0;pwr_state_t state=PWR_UNKNOWN;
        s_data.battery_valid=power_read(&battery,&state);
        s_data.battery=LV_CLAMP(0,battery,100);s_data.charging=state==PWR_CHARGING||state==PWR_FULL;
        s_last_minute=minute;
    }
    if(s_idx%5==3) {
        weather_poll();int temp=0,lo=0,hi=0,code=0,hum=0;
        bool ok=weather_cached(&temp,&lo,&hi,&code,&hum);
        dirty|=ok!=s_data.weather_valid||temp!=s_data.temperature||lo!=s_data.low||hi!=s_data.high||code!=s_data.code||hum!=s_data.humidity;
        s_data.weather_valid=ok;s_data.temperature=temp;s_data.low=lo;s_data.high=hi;s_data.code=code;s_data.humidity=hum;
    }
    if(s_idx%5==4) {
        const lv_image_dsc_t *image=img_store_face_image_for(s_idx/5);
        bool loading=img_store_face_loading(s_idx/5);
        dirty|=image!=s_data.image||loading!=s_data.image_loading;
        s_data.image=image;s_data.image_loading=loading;
    } else {s_data.image=NULL;s_data.image_loading=false;}
    return dirty;
}
static void draw(lv_event_t *e) {
    uintptr_t tag=(uintptr_t)lv_event_get_user_data(e);bool preview=(tag&0x100u)!=0;
    int index=preview?(int)(tag&0xffu):s_idx;lv_area_t a;lv_obj_get_coords(lv_event_get_target_obj(e),&a);
    watchface_data_t data=s_data;if(preview)data.aod=false;
    watchface_render(lv_event_get_layer(e),&a,&data,index,preview);
}
static lv_obj_t *surface(lv_obj_t *parent,int size,uintptr_t tag) {
    lv_obj_t *o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_size(o,size,size);
    lv_obj_set_style_bg_color(o,lv_color_black(),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_set_style_radius(o,LV_RADIUS_CIRCLE,0);ui_obj_set_scrollable(o,false);ui_obj_set_clickable(o,false);
    ui_obj_set_event_bubble(o,true);lv_obj_add_event_cb(o,draw,LV_EVENT_DRAW_MAIN,(void *)tag);return o;
}
static void tick(lv_timer_t *timer) {
    (void)timer;if(snapshot(false)&&wf_content)lv_obj_invalidate(wf_content);
    // AOD refresh is aligned to the next minute, rather than drifting a minute from entry.
    if(wf_timer)lv_timer_set_period(wf_timer,s_aod?(uint32_t)(60-s_data.time.tm_sec)*1000u:1000u);
}
void watchface_select(int index) {
    s_idx=LV_CLAMP(0,index,WATCHFACE_COUNT-1);s_last_minute=(time_t)-1;
    if(watchface_visible()){snapshot(true);lv_obj_invalidate(wf_content);}
}
lv_obj_t *watchface_create_preview(lv_obj_t *parent,int index) {
    index=LV_CLAMP(0,index,WATCHFACE_COUNT-1);snapshot(true);
    return surface(parent,233,0x100u|(uintptr_t)index);
}
void watchface_refresh_preview(lv_obj_t *preview) {if(preview&&snapshot(false))lv_obj_invalidate(preview);}
void watchface_init(void) {
    if(wf_screen)return;
    wf_screen=lv_obj_create(lv_layer_top());lv_obj_remove_style_all(wf_screen);lv_obj_set_size(wf_screen,466,466);
    lv_obj_set_style_bg_color(wf_screen,lv_color_black(),0);lv_obj_set_style_bg_opa(wf_screen,LV_OPA_COVER,0);
    ui_obj_set_scrollable(wf_screen,false);ui_obj_set_gesture_bubble(wf_screen,false);ui_obj_set_hidden(wf_screen,true);
    wf_content=surface(wf_screen,466,0);s_aod=s_sleep=false;watchface_select(settings_face());
}
void watchface_show(void) {
    if(!wf_screen)return;
    quickpanel_hide();s_sleep=false;s_last_minute=(time_t)-1;ui_obj_set_hidden(wf_screen,false);lv_obj_move_foreground(wf_screen);
    if(!wf_timer)wf_timer=lv_timer_create(tick,1000,NULL);
    snapshot(true);tick(NULL);lv_obj_invalidate(wf_content);
}
void watchface_hide(void) {
    if(!wf_screen)return;
    ui_obj_set_hidden(wf_screen,true);if(wf_timer){lv_timer_delete(wf_timer);wf_timer=NULL;}
}
void watchface_set_aod(bool aod) {
    if(aod==s_aod)return;
    s_aod=aod;s_last_minute=(time_t)-1;
    if(watchface_visible()&&!s_sleep){snapshot(true);tick(NULL);lv_obj_invalidate(wf_content);}
}
void watchface_set_sleep(bool sleep) {
    s_sleep=sleep;
    if(sleep){if(wf_timer){lv_timer_delete(wf_timer);wf_timer=NULL;}}
    else if(watchface_visible()){if(!wf_timer)wf_timer=lv_timer_create(tick,1000,NULL);snapshot(true);tick(NULL);lv_obj_invalidate(wf_content);}
}
bool watchface_visible(void){return wf_screen&&!ui_obj_is_hidden(wf_screen);}
lv_obj_t *watchface_root(void){return wf_screen;}
