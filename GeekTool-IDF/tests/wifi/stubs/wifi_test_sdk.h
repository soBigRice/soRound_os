#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include <sys/time.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERROR_CHECK(x) assert((x)==ESP_OK)
#define ESP_LOGI(tag,...) ((void)(tag))
#define WIFI_IF_STA 0
#define WIFI_STORAGE_FLASH 0
#define WIFI_MODE_STA 0
#define WIFI_PS_MAX_MODEM 2
#define ESP_EVENT_ANY_ID -1
#define WIFI_EVENT_STA_START 1
#define WIFI_EVENT_SCAN_DONE 2
#define WIFI_EVENT_STA_DISCONNECTED 3
#define IP_EVENT_STA_GOT_IP 4
#define WIFI_REASON_AUTH_FAIL 202
#define WIFI_REASON_NO_AP_FOUND 201
#define ESP_SNTP_OPMODE_POLL 0
typedef const char *esp_event_base_t;
extern esp_event_base_t WIFI_EVENT,IP_EVENT;
typedef enum {WIFI_AUTH_OPEN,WIFI_AUTH_WEP,WIFI_AUTH_WPA2_PSK} wifi_auth_mode_t;
typedef struct {uint8_t ssid[33];int8_t rssi;wifi_auth_mode_t authmode;} wifi_ap_record_t;
typedef struct {struct {uint8_t ssid[32],password[64];} sta;} wifi_config_t;
typedef struct {bool show_hidden;} wifi_scan_config_t;
typedef struct {int unused;} wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
typedef struct {uint8_t reason;} wifi_event_sta_disconnected_t;
esp_err_t esp_netif_init(void);
esp_err_t esp_event_loop_create_default(void);
void *esp_netif_create_default_wifi_sta(void);
esp_err_t esp_wifi_init(const wifi_init_config_t *);
esp_err_t esp_event_handler_instance_register(esp_event_base_t,int32_t,void (*)(void *,esp_event_base_t,int32_t,void *),void *,void *);
esp_err_t esp_wifi_set_storage(int);
esp_err_t esp_wifi_set_mode(int);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_set_ps(int);
esp_err_t esp_wifi_get_config(int,wifi_config_t *);
esp_err_t esp_wifi_set_config(int,const wifi_config_t *);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_disconnect(void);
esp_err_t esp_wifi_scan_stop(void);
esp_err_t esp_wifi_clear_ap_list(void);
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *,bool);
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *,wifi_ap_record_t *);
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *);
void esp_sntp_setoperatingmode(int);
void esp_sntp_setservername(int,const char *);
void sntp_set_time_sync_notification_cb(void (*)(struct timeval *));
void esp_sntp_init(void);
int64_t esp_timer_get_time(void);
