#pragma once
#include "startup_test_sdk.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#define ESP_ERROR_CHECK(x) assert((x)==ESP_OK)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define pdPASS 1
#define configMAX_PRIORITIES 25
#define ESP_PM_CPU_FREQ_MAX 0
#define ESP_PM_NO_LIGHT_SLEEP 1
#define I2C_NUM_0 0
#define I2C_CLK_SRC_DEFAULT 0
#define WIFI_AUTH_OPEN 0
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(ip) 192u,168u,1u,24u
typedef void *i2c_master_bus_handle_t;
typedef struct {int i2c_port,sda_io_num,scl_io_num,clk_source,glitch_ignore_cnt;struct {bool enable_internal_pullup;} flags;} i2c_master_bus_config_t;
typedef struct {int max_freq_mhz,min_freq_mhz;bool light_sleep_enable;} esp_pm_config_t;
typedef struct {int held;} *esp_pm_lock_handle_t;
typedef int esp_reset_reason_t;
typedef struct {uint8_t ssid[33];int8_t rssi;} wifi_ap_record_t;
typedef struct {int unused;} esp_netif_t;
typedef struct {struct {uint32_t addr;} ip;} esp_netif_ip_info_t;
#include "lvgl.h"
esp_err_t nvs_flash_init(void);
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *,i2c_master_bus_handle_t *);
esp_err_t esp_pm_configure(const esp_pm_config_t *);
esp_reset_reason_t esp_reset_reason(void);
void esp_restart(void);
bool lvgl_port_lock(uint32_t);
void lvgl_port_unlock(void);
int xTaskCreate(void (*fn)(void *),const char *,unsigned,void *,unsigned,void *);
esp_err_t esp_pm_lock_create(int,int,const char *,esp_pm_lock_handle_t *);
esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t);
esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t);
size_t heap_caps_get_free_size(unsigned);
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *);
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *);
esp_err_t esp_netif_get_ip_info(esp_netif_t *,esp_netif_ip_info_t *);
