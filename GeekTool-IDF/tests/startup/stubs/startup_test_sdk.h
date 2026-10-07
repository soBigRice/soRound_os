#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_ERR_OTA_ROLLBACK_FAILED 0x1506
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_SPIRAM 2
#define MALLOC_CAP_8BIT 4
#define NVS_READONLY 0
typedef unsigned nvs_handle_t;
typedef unsigned TickType_t;
#define pdMS_TO_TICKS(ms) (ms)
typedef struct {int dummy;} esp_partition_t;
typedef enum {ESP_OTA_IMG_NEW,ESP_OTA_IMG_PENDING_VERIFY,ESP_OTA_IMG_VALID,ESP_OTA_IMG_INVALID,ESP_OTA_IMG_ABORTED,ESP_OTA_IMG_UNDEFINED=-1} esp_ota_img_states_t;
void *heap_caps_malloc(size_t size,uint32_t caps);
void heap_caps_free(void *memory);
bool heap_caps_check_integrity(uint32_t caps,bool print_errors);
bool esp_psram_is_initialized(void);
int64_t esp_timer_get_time(void);
void vTaskDelay(TickType_t ticks);
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *handle);
void nvs_close(nvs_handle_t handle);
const esp_partition_t *esp_ota_get_running_partition(void);
esp_err_t esp_ota_get_state_partition(const esp_partition_t *partition,esp_ota_img_states_t *state);
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void);
bool esp_ota_check_rollback_is_possible(void);
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void);
