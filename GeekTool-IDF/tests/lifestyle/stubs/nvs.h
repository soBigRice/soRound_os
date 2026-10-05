#pragma once
#include <stdint.h>
typedef int esp_err_t;typedef unsigned nvs_handle_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NVS_NOT_FOUND 2
#define NVS_READONLY 0
#define NVS_READWRITE 1
esp_err_t nvs_open(const char *,int,nvs_handle_t *);
esp_err_t nvs_get_u32(nvs_handle_t,const char *,uint32_t *);
esp_err_t nvs_set_u32(nvs_handle_t,const char *,uint32_t);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
