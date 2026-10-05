#pragma once
#include <stdint.h>
#define ESP_OK 0
typedef struct {uint8_t ssid[33];} wifi_ap_record_t;
int esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap);
