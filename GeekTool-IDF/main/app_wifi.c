// SDK events only update service flags; all LVGL work stays in wifi_tick / input callbacks.
#include "control_ui.h"
#include "settings.h"
#include "board_config.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "rtc.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/time.h>
static const char *TAG="wifi";
#define SCAN_MAX 20
#define CONNECT_TMO 15000
static lv_obj_t *g_panel,*g_list,*g_status,*g_sw,*g_scan,*g_current,*g_ssid,*g_detail;
static lv_obj_t *dlg,*pwd_ta,*kb,*pwd_hint,*pwd_visibility;
static char sel_ssid[33];
static wifi_auth_mode_t sel_auth=WIFI_AUTH_WPA2_PSK;
static volatile bool s_inited,s_scanning,s_scan_done,s_connecting,s_got_ip,s_disconnected,s_suppress_rc;
static volatile bool s_wifi_on=true,s_scan_want;
static volatile uint8_t s_disconnect_reason;
static int s_scan_tries;
static uint32_t connect_start,s_ui_at;
enum { WIFI_IDLE,WIFI_CONNECT_FAILED,WIFI_CONNECT_TIMEOUT,WIFI_START_FAILED };
static int s_result;
typedef struct {char ssid[33];wifi_auth_mode_t auth;int8_t rssi;bool saved;} wifi_row_t;
static wifi_row_t s_rows[SCAN_MAX];
static unsigned s_row_count;
static bool s_have_scan,s_ui_connected;
static void render_rows(void *arg);
static void queue_rows(void){lv_async_call_cancel(render_rows,NULL);lv_async_call(render_rows,NULL);}
static const char *wifi_word(const char *zh,const char *en){return settings_lang()?zh:en;}
static void update_connection(void);
static void close_dialog(void);
static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* ---------- esp_wifi / IP 事件(只设标志,勿碰 LVGL) ---------- */
static void wifi_evt(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            // 开机/重启:若 NVS 里有记住的 AP(FLASH 存储会自动加载),直接自动连
            wifi_config_t wc;
            if (esp_wifi_get_config(WIFI_IF_STA, &wc) == ESP_OK && wc.sta.ssid[0]) esp_wifi_connect();
        } else if (id == WIFI_EVENT_SCAN_DONE) {
            s_scan_done = true;
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            s_got_ip = false;
            s_disconnected = true;
            if(data)s_disconnect_reason=((wifi_event_sta_disconnected_t *)data)->reason;
            // 掉线自动重连;但开关关闭、或正在/将要扫描时不抢射频
            if (s_wifi_on && !s_suppress_rc && !s_scan_want && !s_scanning) esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_got_ip = true;
    }
}

static void on_time_sync(struct timeval *tv) { (void)tv; rtc_save_from_system(); }   // SNTP 校时回调

/* 一次性初始化协议栈(nvs 已在 main 里 init)。多次进入 app 只初始化一次。 */
static void wifi_svc_init(void) {
    if (s_inited) return;
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_evt, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_evt, NULL, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));   // ★凭据写 NVS,重启/重烧后还在
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());                            // → STA_START 事件里自动连记住的 AP
    esp_wifi_set_ps(WIFI_PS_MAX_MODEM);                           // 深度调制解调器睡眠:按 listen interval(默认 3 拍,~300ms)
                                                                  // 醒来收 beacon,比 MIN(每 DTIM)更省;代价是网络延迟略升,
                                                                  // 手表只有天气/SNTP 轮询,无感。关 WiFi 开关仍省最多。

    // SNTP 校时:连上后自动同步(时区在 main 里设为 CST-8),供表盘显示真实时间
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(on_time_sync);   // 校时成功 → 写回 RTC,断电也准
    esp_sntp_init();

    s_inited = true;
}


static void set_status(const char *text,uint32_t color) {
    if(g_status){lv_label_set_text(g_status,text);lv_obj_set_style_text_color(g_status,lv_color_hex(color),0);}
}
static void scan_state(void) {
    if(!g_scan)return;
    bool busy=s_scan_want || s_scanning || s_connecting;
    if(!s_wifi_on || busy)lv_obj_add_state(g_scan,LV_STATE_DISABLED);
    else lv_obj_remove_state(g_scan,LV_STATE_DISABLED);
    lv_label_set_text(lv_obj_get_child(g_scan,0),wifi_word(busy?"扫描中":"重新扫描",busy?"Scanning":"Refresh"));
    if(s_connecting)lv_label_set_text(lv_obj_get_child(g_scan,0),wifi_word("连接中","Connecting"));
}
static void request_scan(void) {
    if(!s_wifi_on || s_connecting || s_scan_want || s_scanning)return;
    s_scan_want=true;s_scan_tries=0;s_scan_done=false;
    // Background scanning keeps the current connection; it never calls disconnect.
    set_status(wifi_word("正在扫描…","Scanning…"),CONTROL_GRAY);scan_state();
}
static void copy_ssid(char out[33],const uint8_t *src) {memcpy(out,src,32);out[32]=0;}
static void connect_state(void) {
    s_got_ip=false;s_disconnected=false;s_disconnect_reason=0;s_result=WIFI_IDLE;
    s_connecting=true;connect_start=now_ms();scan_state();update_connection();
}
static void start_connect(const char *ssid,const char *pass) {
    size_t name_len=strlen(ssid);if(name_len>32)name_len=32;memmove(sel_ssid,ssid,name_len);sel_ssid[name_len]=0;
    wifi_config_t wc={0};size_t n=strlen(ssid);if(n>32)n=32;memcpy(wc.sta.ssid,ssid,n);
    n=pass?strlen(pass):0;if(n>64)return;if(n)memcpy(wc.sta.password,pass,n);
    esp_wifi_scan_stop();s_scan_want=s_scanning=s_scan_done=false;
    s_suppress_rc=true;esp_wifi_disconnect();
    esp_err_t configured=esp_wifi_set_config(WIFI_IF_STA,&wc);s_suppress_rc=false;
    if(configured!=ESP_OK) {
        s_connecting=false;s_result=WIFI_START_FAILED;update_connection();scan_state();return;
    }
    connect_state();
    if(esp_wifi_connect()!=ESP_OK){s_connecting=false;s_result=WIFI_START_FAILED;update_connection();scan_state();}
    ESP_LOGI(TAG,"connecting to %s",ssid);
}
static void reconnect_saved(const char *ssid) {
    size_t name_len=strlen(ssid);if(name_len>32)name_len=32;memmove(sel_ssid,ssid,name_len);sel_ssid[name_len]=0;
    esp_wifi_scan_stop();s_scan_want=s_scanning=s_scan_done=false;s_suppress_rc=false;
    connect_state();
    if(esp_wifi_connect()!=ESP_OK){s_connecting=false;s_result=WIFI_START_FAILED;update_connection();scan_state();}
}
static void close_dialog(void) {
    if(dlg){lv_obj_delete_async(dlg);dlg=NULL;pwd_ta=kb=pwd_hint=pwd_visibility=NULL;}
}
static bool password_valid(const char *pass) {
    size_t n=strlen(pass);
    if(sel_auth==WIFI_AUTH_WEP)return n==5 || n==13 || n==10 || n==26;
    if(n>=8 && n<=63)return true;
    if(n!=64)return false;
    for(size_t i=0;i<n;++i)if(!isxdigit((unsigned char)pass[i]))return false;
    return true;
}
static void connect_btn_event(lv_event_t *e) {
    (void)e;if(!pwd_ta)return;
    const char *value=lv_textarea_get_text(pwd_ta);
    if(!password_valid(value)) {
        lv_label_set_text(pwd_hint,wifi_word("请检查密码长度","Check password length"));
        lv_obj_set_style_text_color(pwd_hint,lv_color_hex(COL_RED),0);return;
    }
    char pass[65],ssid[33];snprintf(pass,sizeof pass,"%s",value);snprintf(ssid,sizeof ssid,"%s",sel_ssid);
    close_dialog();start_connect(ssid,pass);
    // Do not keep another plaintext password copy after passing it to the SDK.
    memset(pass,0,sizeof pass);
}
static void cancel_btn_event(lv_event_t *e){(void)e;close_dialog();}
static void password_gesture(lv_event_t *e) {
    (void)e;lv_indev_t *input=lv_indev_active();
    if(input && lv_indev_get_gesture_dir(input)==LV_DIR_RIGHT)close_dialog();
}
static void password_visibility(lv_event_t *e) {
    (void)e;if(!pwd_ta)return;bool masked=lv_textarea_get_password_mode(pwd_ta);
    lv_textarea_set_password_mode(pwd_ta,!masked);
    lv_label_set_text(lv_obj_get_child(pwd_visibility,0),wifi_word(masked?"隐藏":"显示",masked?"Hide":"Show"));
}
// Native keyboard maps fit the circle; the second symbol page retains all printable ASCII.
static const char *const keys_lower[]={"q","w","e","r","t","y","u","i","o","p","\n",
    "a","s","d","f","g","h","j","k","l","\n",
    "ABC","z","x","c","v","b","n","m",LV_SYMBOL_BACKSPACE,"\n","1#",".","_"," ","-","@",""};
static const char *const keys_upper[]={"Q","W","E","R","T","Y","U","I","O","P","\n",
    "A","S","D","F","G","H","J","K","L","\n",
    "abc","Z","X","C","V","B","N","M",LV_SYMBOL_BACKSPACE,"\n","1#",".","_"," ","-","@",""};
static const char *const keys_symbols[]={"1","2","3","4","5","6","7","8","9","0","\n",
    "@","#","$","%","&","*","(",")","+","=","\n",
    "_","-","/","\\",":",";","\"","'","!","?","\n","abc","#+="," ",".",",",LV_SYMBOL_BACKSPACE,""};
static const char *const keys_extra[]={"[","]","{","}","<",">","^","~","|","`","\n",
    "@","#","$","%","&","*","(",")","+","=","\n",
    "_","-","/","\\",":",";","\"","'","!","?","\n","abc","1#"," ",".",",",LV_SYMBOL_BACKSPACE,""};
#define KEY(w) (LV_BUTTONMATRIX_CTRL_CLICK_TRIG|LV_BUTTONMATRIX_CTRL_NO_REPEAT|(w))
#define BACKSPACE (LV_BUTTONMATRIX_CTRL_CLICK_TRIG|2)
static const lv_buttonmatrix_ctrl_t letter_ctrl[]={KEY(1),KEY(1),KEY(1),KEY(1),KEY(1),KEY(1),KEY(1),KEY(1),KEY(1),KEY(1),
    KEY(1),KEY(1),KEY(1),KEY(1),KEY(1),KEY(1),KEY(1),KEY(1),KEY(1),
    KEY(2),KEY(1),KEY(1),KEY(1),KEY(1),KEY(1),KEY(1),KEY(1),BACKSPACE,
    KEY(2),KEY(1),KEY(1),KEY(4),KEY(1),KEY(1)};
static const lv_buttonmatrix_ctrl_t symbol_ctrl[]={KEY(1),KEY(1),KEY(1),KEY(1),KEY(1),KEY(1),KEY(1),KEY(1),KEY(1),KEY(1),
    KEY(1),KEY(1),KEY(1),KEY(1),KEY(1),KEY(1),KEY(1),KEY(1),KEY(1),KEY(1),
    KEY(1),KEY(1),KEY(1),KEY(1),KEY(1),KEY(1),KEY(1),KEY(1),KEY(1),KEY(1),
    KEY(2),KEY(2),KEY(4),KEY(1),KEY(1),BACKSPACE};
#undef KEY
#undef BACKSPACE
static void kb_event(lv_event_t *e) {
    lv_event_code_t c=lv_event_get_code(e);
    if(c==LV_EVENT_VALUE_CHANGED) {
        uint32_t selected=lv_keyboard_get_selected_button(kb);
        const char *text=lv_keyboard_get_button_text(kb,selected);
        if(text && !strcmp(text,"#+="))lv_keyboard_set_mode(kb,LV_KEYBOARD_MODE_USER_1);
        else lv_keyboard_def_event_cb(e);
    } else if(c==LV_EVENT_READY)connect_btn_event(e);
    else if(c==LV_EVENT_CANCEL)close_dialog();
}
static void show_password_dialog(const char *ssid) {
    if(dlg || !g_panel || !s_wifi_on)return;
    size_t name_len=strlen(ssid);if(name_len>32)name_len=32;memmove(sel_ssid,ssid,name_len);sel_ssid[name_len]=0;
    dlg=control_surface(lv_layer_top(),0,0,466,466);
    lv_obj_set_style_bg_color(dlg,lv_color_black(),0);lv_obj_set_style_bg_opa(dlg,LV_OPA_COVER,0);
    ui_obj_set_gesture_bubble(dlg,false);
    lv_obj_add_event_cb(dlg,password_gesture,LV_EVENT_GESTURE,NULL);
    lv_obj_t *back=control_button(dlg,102,48,44,44,cancel_btn_event,NULL);
    lv_obj_t *arrow=control_label(back,LV_SYMBOL_LEFT,UI_FONT_SYM,0,0,20,CONTROL_WHITE);lv_obj_center(arrow);
    lv_obj_t *title=control_label(dlg,wifi_word("连接网络","Join network"),&font_location_24,153,52,175,CONTROL_WHITE);
    lv_obj_set_style_text_align(title,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_t *name=control_label(dlg,ssid,&font_location_24,86,102,294,CONTROL_WHITE);
    lv_obj_set_style_text_align(name,LV_TEXT_ALIGN_CENTER,0);
    pwd_ta=lv_textarea_create(dlg);lv_textarea_set_one_line(pwd_ta,true);lv_textarea_set_password_mode(pwd_ta,true);
    lv_textarea_set_password_show_time(pwd_ta,0);lv_textarea_set_max_length(pwd_ta,64);
    lv_textarea_set_placeholder_text(pwd_ta,wifi_word("密码","Password"));lv_obj_set_pos(pwd_ta,74,144);lv_obj_set_size(pwd_ta,242,48);
    lv_obj_set_style_text_font(pwd_ta,control_small_font(),0);lv_obj_set_style_text_color(pwd_ta,lv_color_hex(CONTROL_WHITE),0);
    lv_obj_set_style_bg_color(pwd_ta,lv_color_hex(CONTROL_CARD),0);lv_obj_set_style_border_color(pwd_ta,lv_color_hex(CONTROL_LINE),0);
    lv_obj_set_style_border_width(pwd_ta,1,0);lv_obj_set_style_radius(pwd_ta,12,0);ui_obj_set_gesture_bubble(pwd_ta,false);
    pwd_visibility=control_button(dlg,324,144,68,48,password_visibility,NULL);
    control_button_text(pwd_visibility,wifi_word("显示","Show"));
    lv_obj_t *visibility_label=lv_obj_get_child(pwd_visibility,0);
    lv_obj_set_style_text_font(visibility_label,control_small_font(),0);
    lv_obj_set_height(visibility_label,control_small_font()->line_height);lv_obj_center(visibility_label);
    wifi_config_t remembered;char remembered_ssid[33];bool have_password=false;
    if(esp_wifi_get_config(WIFI_IF_STA,&remembered)==ESP_OK){
        copy_ssid(remembered_ssid,remembered.sta.ssid);
        if(!strcmp(remembered_ssid,sel_ssid) && remembered.sta.password[0]){
            char pass[65];memcpy(pass,remembered.sta.password,64);pass[64]=0;
            lv_textarea_set_text(pwd_ta,pass);memset(pass,0,sizeof pass);have_password=true;
        }
    }
    pwd_hint=control_label(dlg,wifi_word(have_password?"已记住密码，可直接连接":"输入密码后点连接",have_password?"Saved password · connect":"Enter password, then connect"),control_small_font(),86,198,294,CONTROL_GRAY);
    lv_obj_set_style_text_align(pwd_hint,LV_TEXT_ALIGN_CENTER,0);
    kb=lv_keyboard_create(dlg);lv_obj_set_size(kb,328,160);
    // The native keyboard defaults to bottom alignment; set_pos alone would add offsets to that anchor.
    lv_obj_align(kb,LV_ALIGN_TOP_LEFT,69,226);
    lv_obj_set_style_bg_opa(kb,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(kb,0,0);
    lv_obj_set_style_pad_all(kb,0,0);lv_obj_set_style_pad_row(kb,4,0);lv_obj_set_style_pad_column(kb,3,0);
    lv_obj_set_style_text_font(kb,UI_FONT_SYM,LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb,lv_color_hex(CONTROL_CARD),LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb,lv_color_hex(COL_RED),LV_PART_ITEMS|LV_STATE_PRESSED);
    lv_obj_set_style_text_color(kb,lv_color_hex(CONTROL_WHITE),LV_PART_ITEMS);lv_obj_set_style_radius(kb,7,LV_PART_ITEMS);
    lv_keyboard_set_map(kb,LV_KEYBOARD_MODE_TEXT_LOWER,keys_lower,letter_ctrl);
    lv_keyboard_set_map(kb,LV_KEYBOARD_MODE_TEXT_UPPER,keys_upper,letter_ctrl);
    lv_keyboard_set_map(kb,LV_KEYBOARD_MODE_SPECIAL,keys_symbols,symbol_ctrl);
    lv_keyboard_set_map(kb,LV_KEYBOARD_MODE_USER_1,keys_extra,symbol_ctrl);
    lv_keyboard_set_mode(kb,LV_KEYBOARD_MODE_TEXT_LOWER);lv_keyboard_set_textarea(kb,pwd_ta);
    ui_obj_set_gesture_bubble(kb,false);lv_obj_remove_event_cb(kb,lv_keyboard_def_event_cb);
    lv_obj_add_event_cb(kb,kb_event,LV_EVENT_ALL,NULL);
    lv_obj_t *connect=control_button(dlg,151,396,164,44,connect_btn_event,NULL);
    lv_obj_set_style_bg_color(connect,lv_color_hex(COL_RED),0);control_button_text(connect,wifi_word("连接","Connect"));
}
static void wifi_row_click(lv_event_t *e) {
    if(s_connecting || !s_wifi_on)return;
    const wifi_row_t *row=lv_event_get_user_data(e);sel_auth=row->auth;
    if(row->saved)reconnect_saved(row->ssid);
    else if(row->auth!=WIFI_AUTH_OPEN)show_password_dialog(row->ssid);
    else start_connect(row->ssid,"");
}
static void render_rows(void *arg) {
    (void)arg;if(!g_list)return;lv_obj_clean(g_list);if(!s_wifi_on)return;
    wifi_ap_record_t current;bool connected=s_got_ip && esp_wifi_sta_get_ap_info(&current)==ESP_OK;
    char current_ssid[33]={0};if(connected)copy_ssid(current_ssid,current.ssid);
    wifi_config_t wc;bool saved=esp_wifi_get_config(WIFI_IF_STA,&wc)==ESP_OK && wc.sta.ssid[0];
    char saved_ssid[33]={0};if(saved)copy_ssid(saved_ssid,wc.sta.ssid);
    unsigned visible=0;
    for(unsigned i=0;i<s_row_count;++i) {
        wifi_row_t *entry=&s_rows[i];if(connected && !strcmp(entry->ssid,current_ssid))continue;
        entry->saved=saved && !strcmp(saved_ssid,entry->ssid);++visible;
        lv_obj_t *row=control_button(g_list,0,0,324,52,wifi_row_click,entry);
        control_label(row,entry->ssid,&font_location_24,14,0,270,CONTROL_WHITE);
        const char *caption=entry->saved?wifi_word("已保存","Saved"):
            (entry->auth==WIFI_AUTH_OPEN?wifi_word("开放网络","Open network"):wifi_word("需要密码","Password required"));
        control_label(row,caption,control_small_font(),14,28,216,CONTROL_GRAY);
        char bars[8];snprintf(bars,sizeof bars,"%d",entry->rssi);
        control_label(row,bars,control_small_font(),270,28,46,CONTROL_GRAY);
    }
    if(!visible) {
        lv_obj_t *label=control_label(g_list,wifi_word(connected?"没有其他网络":"未发现网络",connected?"No other networks":"No networks found"),control_small_font(),0,0,314,CONTROL_GRAY);
        lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
    }
    lv_obj_update_layout(g_list);
}
static void wifi_populate(void) {
    if(!g_list)return;
    s_row_count=0;
    wifi_ap_record_t recs[SCAN_MAX];uint16_t n=SCAN_MAX;
    esp_err_t result=esp_wifi_scan_get_ap_records(&n,recs);
    if(result!=ESP_OK){n=0;esp_wifi_clear_ap_list();}
    for(unsigned i=0;i<n;++i) {
        char ssid[33];copy_ssid(ssid,recs[i].ssid);if(!ssid[0])continue;
        bool duplicate=false;for(unsigned j=0;j<s_row_count;++j)if(!strcmp(s_rows[j].ssid,ssid))duplicate=true;
        if(duplicate)continue;
        wifi_row_t *entry=&s_rows[s_row_count++];snprintf(entry->ssid,sizeof entry->ssid,"%s",ssid);
        entry->auth=recs[i].authmode;entry->rssi=recs[i].rssi;entry->saved=false;
    }
    s_have_scan=true;render_rows(NULL);
    set_status(wifi_word(result==ESP_OK?"附近网络":"扫描失败，请重试",result==ESP_OK?"Nearby networks":"Scan failed, retry"),result==ESP_OK?CONTROL_GRAY:COL_RED);
}
static void current_click(lv_event_t *e) {
    (void)e;if(s_connecting || !s_wifi_on)return;
    wifi_ap_record_t current;if(esp_wifi_sta_get_ap_info(&current)==ESP_OK)return;
    if(s_result!=WIFI_IDLE && sel_ssid[0]) {
        if(sel_auth==WIFI_AUTH_OPEN)start_connect(sel_ssid,"");else show_password_dialog(sel_ssid);
    } else {
        wifi_config_t wc;if(esp_wifi_get_config(WIFI_IF_STA,&wc)==ESP_OK && wc.sta.ssid[0]) {
            char ssid[33];copy_ssid(ssid,wc.sta.ssid);reconnect_saved(ssid);
        }
    }
}
static void update_connection(void) {
    if(!g_current)return;
    wifi_ap_record_t ap;bool connected=s_wifi_on && esp_wifi_sta_get_ap_info(&ap)==ESP_OK && s_got_ip;
    char name[33],detail[100];uint32_t color=CONTROL_GRAY;
    if(!s_wifi_on){snprintf(name,sizeof name,"%s",wifi_word("Wi-Fi已关闭","Wi-Fi is off"));snprintf(detail,sizeof detail,"%s",wifi_word("开启后自动连接","Enable to connect"));}
    else if(connected){copy_ssid(name,ap.ssid);snprintf(detail,sizeof detail,wifi_word("已连接 · %d dBm","Connected · %d dBm"),ap.rssi);color=CONTROL_WHITE;s_result=WIFI_IDLE;}
    else if(s_connecting){snprintf(name,sizeof name,"%s",sel_ssid);snprintf(detail,sizeof detail,"%s",wifi_word("正在连接…","Connecting…"));}
    else if(s_result!=WIFI_IDLE){snprintf(name,sizeof name,"%s",sel_ssid);color=COL_RED;
        const char *message=s_result==WIFI_CONNECT_TIMEOUT?wifi_word("连接超时，点按重试","Timed out · tap to retry"):
            wifi_word("连接失败，点按重试","Failed · tap to retry");
        if(s_result==WIFI_CONNECT_FAILED && s_disconnect_reason==WIFI_REASON_AUTH_FAIL)message=wifi_word("认证失败，点按修改密码","Auth failed · edit password");
        else if(s_result==WIFI_CONNECT_FAILED && s_disconnect_reason==WIFI_REASON_NO_AP_FOUND)message=wifi_word("未找到网络，点按重试","Network not found · retry");
        snprintf(detail,sizeof detail,"%s",message);
    } else {wifi_config_t wc;
        if(esp_wifi_get_config(WIFI_IF_STA,&wc)==ESP_OK && wc.sta.ssid[0]){copy_ssid(name,wc.sta.ssid);snprintf(detail,sizeof detail,"%s",wifi_word("已保存 · 点按连接","Saved · tap to connect"));}
        else {snprintf(name,sizeof name,"%s",wifi_word("尚未连接","Not connected"));snprintf(detail,sizeof detail,"%s",wifi_word("选择下方网络","Select a network below"));}
    }
    bool changed=strcmp(lv_label_get_text(g_ssid),name)!=0 || connected!=s_ui_connected;
    s_ui_connected=connected;
    if(changed){lv_label_set_text(g_ssid,name);if(s_have_scan)queue_rows();}
    if(strcmp(lv_label_get_text(g_detail),detail))lv_label_set_text(g_detail,detail);
    lv_obj_set_style_text_color(g_detail,lv_color_hex(color),0);
    scan_state();
}
void wifi_service_set_enabled(bool on) {
    if(!s_inited)wifi_svc_init();
    s_wifi_on=on;
    if(on){s_suppress_rc=false;s_result=WIFI_IDLE;s_disconnect_reason=0;wifi_config_t wc;if(esp_wifi_get_config(WIFI_IF_STA,&wc)==ESP_OK && wc.sta.ssid[0])esp_wifi_connect();}
    else {s_scan_want=s_scanning=s_scan_done=s_connecting=false;s_suppress_rc=true;s_got_ip=false;
        esp_wifi_scan_stop();esp_wifi_disconnect();close_dialog();if(g_list)lv_obj_clean(g_list);
    }
    if(g_sw){if(on)lv_obj_add_state(g_sw,LV_STATE_CHECKED);else lv_obj_remove_state(g_sw,LV_STATE_CHECKED);}
    update_connection();scan_state();
}
bool wifi_service_enabled(void){return s_wifi_on;}
static void wifi_sw_cb(lv_event_t *e) {
    bool on=lv_obj_has_state(lv_event_get_target_obj(e),LV_STATE_CHECKED);wifi_service_set_enabled(on);
    if(on)request_scan();else set_status(wifi_word("无线网络已关闭","Wireless is off"),CONTROL_GRAY);
}
static void wifi_scan_cb(lv_event_t *e){(void)e;request_scan();}
static void wifi_enter(lv_obj_t *parent) {
    wifi_svc_init();s_have_scan=false;s_ui_connected=false;s_row_count=0;g_panel=control_surface(parent,0,0,466,466);launcher_set_title("Wi-Fi");
    g_sw=control_toggle(g_panel,80,102,306,wifi_word("无线网络","Wireless"),wifi_word("2.4 GHz 网络","2.4 GHz networks"),s_wifi_on,wifi_sw_cb,NULL);
    lv_obj_t *power_row=lv_obj_get_parent(g_sw);lv_obj_set_height(power_row,54);
    lv_obj_set_y(lv_obj_get_child(power_row,0),12);ui_obj_set_hidden(lv_obj_get_child(power_row,1),true);lv_obj_set_y(g_sw,11);
    g_current=control_button(g_panel,58,160,350,66,current_click,NULL);
    g_ssid=control_label(g_current,"",&font_location_24,18,3,314,CONTROL_WHITE);
    g_detail=control_label(g_current,"",control_small_font(),18,35,314,CONTROL_GRAY);
    g_status=control_label(g_panel,wifi_word("附近网络","Nearby networks"),control_small_font(),76,240,178,CONTROL_GRAY);
    g_scan=control_button(g_panel,272,228,118,44,wifi_scan_cb,NULL);control_button_text(g_scan,wifi_word("重新扫描","Refresh"));
    lv_obj_set_style_text_font(lv_obj_get_child(g_scan,0),control_small_font(),0);
    lv_obj_set_height(lv_obj_get_child(g_scan,0),control_small_font()->line_height);lv_obj_center(lv_obj_get_child(g_scan,0));
    g_list=control_surface(g_panel,71,280,324,110);
    ui_obj_set_scrollable(g_list,true);lv_obj_set_scroll_dir(g_list,LV_DIR_VER);
    lv_obj_set_flex_flow(g_list,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_row(g_list,6,0);
    lv_obj_set_scrollbar_mode(g_list,LV_SCROLLBAR_MODE_AUTO);lv_obj_set_style_width(g_list,3,LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(g_list,lv_color_hex(CONTROL_GRAY),LV_PART_SCROLLBAR);
    s_ui_at=0;update_connection();if(s_wifi_on)request_scan();else set_status(wifi_word("无线网络已关闭","Wireless is off"),CONTROL_GRAY);
}
static void wifi_tick(void) {
    if(s_scan_want && !s_scanning) {
        wifi_scan_config_t sc={.show_hidden=true};
        if(esp_wifi_scan_start(&sc,false)==ESP_OK){s_scanning=true;s_scan_want=false;}
        else if(++s_scan_tries>24){s_scan_want=false;set_status(wifi_word("扫描失败，请重试","Scan failed, retry"),COL_RED);scan_state();}
    }
    if(s_scanning && s_scan_done) {
        s_scanning=s_scan_done=false;wifi_populate();scan_state();
        wifi_ap_record_t ap;if(s_wifi_on && esp_wifi_sta_get_ap_info(&ap)!=ESP_OK){wifi_config_t wc;
            if(esp_wifi_get_config(WIFI_IF_STA,&wc)==ESP_OK && wc.sta.ssid[0])esp_wifi_connect();}
    }
    if(s_connecting) {
        if(s_got_ip){s_connecting=false;s_result=WIFI_IDLE;update_connection();}
        else if(s_disconnected && now_ms()-connect_start>2000){s_connecting=false;s_result=WIFI_CONNECT_FAILED;update_connection();}
        else if(now_ms()-connect_start>CONNECT_TMO){s_connecting=false;s_result=WIFI_CONNECT_TIMEOUT;update_connection();}
    }
    if(!s_ui_at || now_ms()-s_ui_at>=1000){s_ui_at=now_ms();update_connection();}
}
static bool wifi_back(void){if(!dlg)return false;close_dialog();return true;}
static void wifi_exit(void) {
    lv_async_call_cancel(render_rows,NULL);close_dialog();if(s_scanning || s_scan_want){esp_wifi_scan_stop();esp_wifi_clear_ap_list();}
    s_scanning=s_scan_want=s_scan_done=false;
    g_panel=g_list=g_status=g_sw=g_scan=g_current=g_ssid=g_detail=NULL;s_row_count=0;s_have_scan=false;
    // The service and saved credentials remain alive when leaving the page.
}
void wifi_service_start(void){wifi_svc_init();}
const app_t app_wifi={.name="WiFi",.color=COL_WIFI,.enter=wifi_enter,.tick=wifi_tick,.exit=wifi_exit,.back=wifi_back};
