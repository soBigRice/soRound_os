// Native LVGL pages with a fake radio. No network requests or saved device credentials.
#include "wifi_test_sdk.h"
#include "control_ui.h"
#include "src/misc/lv_text_private.h"
#include "src/misc/lv_area_private.h"
#include "src/widgets/buttonmatrix/lv_buttonmatrix_private.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
esp_event_base_t WIFI_EVENT="wifi",IP_EVENT="ip";
static uint8_t language;
uint8_t settings_lang(void){return language;}
static wifi_config_t config;
static wifi_ap_record_t current,records[20];
static unsigned record_count;
static bool connected,synchronous_ip;
static bool netif_present=true,netif_up=true,ip_error;
static uint32_t ipv4=0x01020304;
static esp_netif_t fake_netif;
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key){assert(!strcmp(key,"WIFI_STA_DEF"));return netif_present?&fake_netif:NULL;}
bool esp_netif_is_netif_up(esp_netif_t *n){assert(n==&fake_netif);return netif_up;}
esp_err_t esp_netif_get_ip_info(esp_netif_t *n,esp_netif_ip_info_t *ip){assert(n==&fake_netif);ip->ip.addr=ipv4;return ip_error?ESP_FAIL:ESP_OK;}
static int init_count,connects,disconnects,config_writes,scan_starts,scan_clears;
static esp_err_t scan_result=ESP_OK,connect_result=ESP_OK,records_result=ESP_OK;
static int64_t clock_us=50000;
static lv_obj_t *heading;
void launcher_set_title(const char *text){lv_label_set_text(heading,text);}
void rtc_save_from_system(void){}
int64_t esp_timer_get_time(void){return clock_us;}
esp_err_t esp_netif_init(void){return ESP_OK;}
esp_err_t esp_event_loop_create_default(void){return ESP_OK;}
void *esp_netif_create_default_wifi_sta(void){return &config;}
esp_err_t esp_wifi_init(const wifi_init_config_t *c){(void)c;++init_count;return ESP_OK;}
esp_err_t esp_event_handler_instance_register(esp_event_base_t b,int32_t id,void (*cb)(void *,esp_event_base_t,int32_t,void *),void *a,void *h){(void)b;(void)id;(void)cb;(void)a;(void)h;return ESP_OK;}
esp_err_t esp_wifi_set_storage(int n){(void)n;return ESP_OK;}
esp_err_t esp_wifi_set_mode(int n){(void)n;return ESP_OK;}
esp_err_t esp_wifi_start(void){return ESP_OK;}
esp_err_t esp_wifi_set_ps(int n){(void)n;return ESP_OK;}
esp_err_t esp_wifi_get_config(int n,wifi_config_t *c){(void)n;*c=config;return ESP_OK;}
esp_err_t esp_wifi_set_config(int n,const wifi_config_t *c){(void)n;config=*c;++config_writes;return ESP_OK;}
esp_err_t esp_wifi_disconnect(void){connected=false;++disconnects;return ESP_OK;}
esp_err_t esp_wifi_scan_stop(void){return ESP_OK;}
esp_err_t esp_wifi_clear_ap_list(void){++scan_clears;return ESP_OK;}
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *c,bool block){assert(c->show_hidden && !block);++scan_starts;return scan_result;}
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *n,wifi_ap_record_t *r){if(records_result!=ESP_OK)return records_result;if(*n>record_count)*n=record_count;memcpy(r,records,*n*sizeof *r);return ESP_OK;}
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *a){if(!connected)return ESP_FAIL;*a=current;return ESP_OK;}
void esp_sntp_setoperatingmode(int n){(void)n;}
void esp_sntp_setservername(int n,const char *v){(void)n;assert(!strcmp(v,"pool.ntp.org"));}
void sntp_set_time_sync_notification_cb(void (*cb)(struct timeval *)){assert(cb);}
static unsigned sntp_starts,sntp_restarts;
void esp_sntp_init(void){++sntp_starts;}
bool esp_sntp_restart(void){++sntp_restarts;return true;}
#include "../../main/app_wifi.c"
esp_err_t esp_wifi_connect(void){
    ++connects;if(connect_result!=ESP_OK)return connect_result;
    if(synchronous_ip){copy_ssid((char *)current.ssid,config.sta.ssid);current.rssi=-48;connected=true;wifi_evt(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);}
    return ESP_OK;
}
#define W 466
_Alignas(LV_DRAW_BUF_ALIGN) static uint16_t buffer[W*48];
static uint16_t frame[W*W];
static lv_display_t *display;
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *map){
    for(int y=a->y1;y<=a->y2;++y){memcpy(frame+y*W+a->x1,map,(size_t)lv_area_get_width(a)*2);map+=lv_area_get_width(a)*2;}
    lv_display_flush_ready(d);
}
static void labels(lv_obj_t *o,const lv_area_t *clip){
    if(ui_obj_is_hidden(o))return;lv_area_t a;lv_obj_get_coords(o,&a);
    lv_area_t visible;if(!lv_area_intersect(&visible,&a,clip))return;
    if(lv_obj_check_type(o,&lv_label_class)){
        lv_obj_t *parent=lv_obj_get_parent(o);
        if(lv_obj_check_type(parent,&lv_button_class)){
            lv_area_t p;lv_obj_get_coords(parent,&p);
            assert(a.x1>=p.x1 && a.x2<=p.x2 && a.y1>=p.y1 && a.y2<=p.y2);
        }
        const char *s=lv_label_get_text(o);const lv_font_t *font=lv_obj_get_style_text_font(o,0);uint32_t at=0;
        while(s[at]){uint32_t cp=lv_text_encoded_next(s,&at);lv_font_glyph_dsc_t g;
            if(!lv_font_get_glyph_dsc(font,&g,cp,0) || g.is_placeholder){fprintf(stderr,"missing glyph U+%04x in %s\n",cp,s);abort();}}
        for(unsigned i=0;i<4;++i){int x=(i&1)?visible.x2:visible.x1,y=(i&2)?visible.y2:visible.y1;
            if(hypot(x-232.5,y-232.5)>228){fprintf(stderr,"outside circle: %s (%d,%d)\n",s,x,y);abort();}}
    }
    for(uint32_t i=0;i<lv_obj_get_child_count(o);++i)labels(lv_obj_get_child(o,i),&visible);
}
static void capture(const char *directory,const char *name){
    lv_obj_update_layout(lv_screen_active());lv_obj_update_layout(lv_layer_top());
    lv_area_t bounds={0,0,465,465};labels(lv_screen_active(),&bounds);labels(lv_layer_top(),&bounds);
    lv_tick_inc(20);lv_timer_handler();lv_refr_now(display);if(!directory)return;
    char path[512];snprintf(path,sizeof path,"%s/%s-%s.ppm",directory,name,language?"zh":"en");
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n466 466\n255\n");
    for(int i=0;i<W*W;++i){uint16_t p=frame[i];uint8_t rgb[]={((p>>11)&31)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};fwrite(rgb,1,3,f);}assert(!fclose(f));
}
static void scan_complete(void){wifi_tick();assert(s_scanning);wifi_evt(NULL,WIFI_EVENT,WIFI_EVENT_SCAN_DONE,NULL);wifi_tick();assert(!s_scanning);}
static void key(const char *text){
    for(uint32_t i=0;;++i){const char *t=lv_keyboard_get_button_text(kb,i);assert(t);
        if(!strcmp(t,text)){lv_buttonmatrix_set_selected_button(kb,i);lv_obj_send_event(kb,LV_EVENT_VALUE_CHANGED,NULL);return;}}
}
static lv_obj_t *page(void){return control_surface(lv_screen_active(),0,0,466,466);}
int main(int argc,char **argv){
    const char *directory=argc>1?argv[1]:NULL;lv_init();i18n_init();display=lv_display_create(W,W);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);lv_display_set_buffers(display,buffer,NULL,sizeof buffer,LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
    lv_obj_set_style_bg_color(lv_screen_active(),lv_color_black(),0);
    lv_obj_t *ring=lv_arc_create(lv_layer_top());lv_obj_set_size(ring,458,458);lv_obj_center(ring);
    lv_arc_set_rotation(ring,270);lv_arc_set_bg_angles(ring,0,360);lv_arc_set_range(ring,0,100);lv_arc_set_value(ring,74);
    lv_obj_remove_style(ring,NULL,LV_PART_KNOB);lv_obj_set_style_arc_width(ring,8,LV_PART_MAIN);lv_obj_set_style_arc_width(ring,8,LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ring,lv_color_hex(0x15151a),LV_PART_MAIN);lv_obj_set_style_arc_color(ring,lv_color_hex(CONTROL_WHITE),LV_PART_INDICATOR);
    heading=control_label(lv_layer_top(),"Wi-Fi",&font_location_24,150,52,166,CONTROL_WHITE);lv_obj_set_style_text_align(heading,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_t *back=control_button(lv_layer_top(),101,52,44,44,NULL,NULL);lv_obj_t *arrow=control_label(back,LV_SYMBOL_LEFT,UI_FONT_SYM,0,0,20,CONTROL_WHITE);lv_obj_center(arrow);
    wifi_service_start();assert(sntp_starts==0);
    connected=true;s_got_ip=false;assert(!wifi_service_ready());
    wifi_evt(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);assert(wifi_service_ready());
    assert(sntp_starts==1 && sntp_restarts==0);
    ipv4=0;assert(!wifi_service_ready());ipv4=0x01020304;
    netif_up=false;assert(!wifi_service_ready());netif_up=true;
    netif_present=false;assert(!wifi_service_ready());netif_present=true;
    ip_error=true;assert(!wifi_service_ready());ip_error=false;
    wifi_evt(NULL,IP_EVENT,IP_EVENT_STA_LOST_IP,NULL);assert(!wifi_service_ready());
    wifi_evt(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);assert(wifi_service_ready());
    assert(sntp_starts==1 && sntp_restarts==1);
    wifi_evt(NULL,WIFI_EVENT,WIFI_EVENT_STA_STOP,NULL);assert(!wifi_service_ready());
    for(language=0;language<2;++language){
        s_result=WIFI_IDLE;s_connecting=false;s_wifi_on=true;s_got_ip=true;connected=true;
        strcpy((char *)current.ssid,"Studio_2.4G");current.rssi=-48;current.authmode=WIFI_AUTH_WPA2_PSK;strcpy((char *)config.sta.ssid,"Studio_2.4G");
        records[0]=current;strcpy((char *)records[1].ssid,"Home_2.4G");records[1].authmode=WIFI_AUTH_WPA2_PSK;records[1].rssi=-56;
        records[2]=records[1];strcpy((char *)records[3].ssid,"Cafe Guest");records[3].authmode=WIFI_AUTH_OPEN;records[3].rssi=-70;record_count=4;
        int before=disconnects;lv_obj_t *root=page();wifi_enter(root);scan_complete();assert(disconnects==before && s_row_count==3 && lv_obj_get_child_count(g_list)==2);
        assert(!strcmp(s_rows[1].ssid,"Home_2.4G"));capture(directory,"wifi-connected");
        lv_area_t list,scan,card;lv_obj_get_coords(g_list,&list);lv_obj_get_coords(g_scan,&scan);lv_obj_get_coords(g_current,&card);
        assert(card.y2<scan.y1 && scan.y2<list.y1 && lv_obj_get_height(g_scan)>=44);
        lv_obj_send_event(lv_obj_get_child(g_list,0),LV_EVENT_CLICKED,NULL);
        assert(dlg && lv_textarea_get_password_mode(pwd_ta) && disconnects==before);capture(directory,"wifi-password");
        lv_area_t keyboard;lv_obj_get_coords(kb,&keyboard);lv_buttonmatrix_t *matrix=(lv_buttonmatrix_t *)kb;
        for(uint32_t j=0;j<matrix->btn_cnt;++j){lv_area_t a=matrix->button_areas[j];assert(lv_area_get_width(&a)>=26 && lv_area_get_height(&a)>=34);
            for(unsigned k=0;k<4;++k){int x=keyboard.x1+((k&1)?a.x2:a.x1),y=keyboard.y1+((k&2)?a.y2:a.y1);assert(hypot(x-232.5,y-232.5)<=228);}}

        key("q");key("ABC");key("A");key(LV_SYMBOL_BACKSPACE);assert(!strcmp(lv_textarea_get_text(pwd_ta),"q"));
        key("1#");key("#+=");assert(lv_keyboard_get_mode(kb)==LV_KEYBOARD_MODE_USER_1);key("~");assert(!strcmp(lv_textarea_get_text(pwd_ta),"q~"));
        bool ascii[127]={0};const lv_keyboard_mode_t modes[]={LV_KEYBOARD_MODE_TEXT_LOWER,LV_KEYBOARD_MODE_TEXT_UPPER,LV_KEYBOARD_MODE_SPECIAL,LV_KEYBOARD_MODE_USER_1};
        for(unsigned m=0;m<4;++m){lv_keyboard_set_mode(kb,modes[m]);const char *const *map=lv_keyboard_get_map_array(kb);
            for(unsigned j=0;map[j][0];++j)if(strlen(map[j])==1 && (unsigned char)map[j][0]>=32 && (unsigned char)map[j][0]<127)ascii[(unsigned char)map[j][0]]=true;}
        for(unsigned c=32;c<127;++c)assert(ascii[c]);
        lv_textarea_set_text(pwd_ta,"short");before=config_writes;connect_btn_event(NULL);assert(dlg && config_writes==before);
        lv_textarea_set_text(pwd_ta,"fixture-password");synchronous_ip=true;connect_btn_event(NULL);lv_timer_handler();
        assert(!dlg && s_got_ip && config_writes==before+1);wifi_tick();lv_timer_handler();assert(!s_connecting && !strcmp(lv_label_get_text(g_ssid),"Home_2.4G"));
        assert(lv_obj_get_child_count(g_list)==2 && strcmp(lv_label_get_text(lv_obj_get_child(lv_obj_get_child(g_list,0),0)),"Home_2.4G"));
        capture(directory,"wifi-new-connected");synchronous_ip=false;
        before=config_writes;show_password_dialog("Home_2.4G");assert(wifi_back());lv_timer_handler();assert(!dlg && config_writes==before && !wifi_back());
        connected=false;start_connect("Home_2.4G","fixture-password");wifi_event_sta_disconnected_t reason={.reason=WIFI_REASON_AUTH_FAIL};
        wifi_evt(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,&reason);clock_us+=3000000;wifi_tick();assert(s_result==WIFI_CONNECT_FAILED);capture(directory,"wifi-failed");
        lv_obj_send_event(g_current,LV_EVENT_CLICKED,NULL);assert(dlg && !strcmp(sel_ssid,"Home_2.4G") && !strcmp(lv_textarea_get_text(pwd_ta),"fixture-password"));assert(wifi_back());lv_timer_handler();
        start_connect("Home_2.4G","fixture-password");clock_us+=16000000;wifi_tick();assert(s_result==WIFI_CONNECT_TIMEOUT);capture(directory,"wifi-timeout");
        // Full 32-byte SSID and 64-byte hexadecimal PSK must reach the SDK intact.
        const char *full="12345678901234567890123456789012";char hex[65];memset(hex,'a',64);hex[64]=0;
        sel_auth=WIFI_AUTH_WPA2_PSK;assert(password_valid(hex));start_connect(full,hex);assert(!memcmp(config.sta.ssid,full,32) && !memcmp(config.sta.password,hex,64));
        capture(directory,"wifi-long-ssid");assert(lv_obj_get_height(g_ssid)==font_location_24.line_height);
        s_connecting=false;connect_result=ESP_FAIL;reconnect_saved(full);assert(s_result==WIFI_START_FAILED);connect_result=ESP_OK;
        before=config_writes;reconnect_saved(full);assert(config_writes==before && !dlg);
        s_connecting=false;s_result=WIFI_IDLE;show_password_dialog("Home_2.4G");before=disconnects;
        lv_obj_send_event(lv_obj_get_parent(g_sw),LV_EVENT_CLICKED,NULL);lv_timer_handler();
        assert(!wifi_service_enabled() && !dlg && disconnects==before+1 && lv_obj_has_state(g_scan,LV_STATE_DISABLED));capture(directory,"wifi-off");
        before=scan_starts;request_scan();wifi_tick();assert(scan_starts==before);
        wifi_service_set_enabled(true);s_got_ip=true;connected=true;
        scan_result=ESP_FAIL;before=disconnects;request_scan();for(int i=0;i<25;++i)wifi_tick();
        assert(!s_scan_want && !s_scanning && disconnects==before && !lv_obj_has_state(g_scan,LV_STATE_DISABLED));capture(directory,"wifi-scan-failed");scan_result=ESP_OK;
        records_result=ESP_FAIL;before=scan_clears;request_scan();scan_complete();assert(scan_clears==before+1 && s_row_count==0);records_result=ESP_OK;
        before=disconnects;wifi_exit();lv_obj_delete(root);lv_timer_handler();assert(disconnects==before && !g_panel && !dlg);
    }
    // Leaving while a scan is pending releases scan records without disconnecting the radio.
    language=0;for(unsigned i=0;i<12;++i){lv_obj_t *root=page();wifi_enter(root);wifi_tick();assert(s_scanning);
        int before=disconnects;wifi_exit();lv_obj_delete(root);lv_timer_handler();assert(disconnects==before && !g_list && !dlg);}
    assert(init_count==1 && scan_clears>=12 && lv_obj_get_child_count(lv_layer_top())==3);
    puts("Wi-Fi native EN/ZH layout, background scan, deduplication, masked/cancelled passwords, ASCII keyboard, saved reconnect, full SSID/PSK, immediate IP, auth/timeout feedback, enable/exit cleanup passed");
    lv_deinit();return 0;
}
