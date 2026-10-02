#pragma once
// 仅替换 ESP-IDF 硬件/网络 API;ota_update.c 使用真实实现,故障由测试注入。
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_INVALID_RESPONSE 0x108
#define ESP_ERR_INVALID_VERSION 0x10a
#define ESP_ERR_HTTP_CONNECT 0x7002
#define ESP_ERR_HTTP_FETCH_HEADER 0x7004
#define ESP_ERR_HTTP_CONNECTION_CLOSED 0x7008
#define ESP_ERR_HTTP_EAGAIN 0x7007
#define ESP_ERR_OTA_VALIDATE_FAILED 0x1503
#define ESP_ERR_HTTPS_OTA_IN_PROGRESS 0x9001
#define MBEDTLS_ERR_X509_CERT_VERIFY_FAILED (-0x2700)
#define MBEDTLS_ERR_SSL_ALLOC_FAILED (-0x7f00)
#define MBEDTLS_ERR_X509_ALLOC_FAILED (-0x2880)
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
#define ESP_APP_DESC_MAGIC_WORD 0xabcd5432u
typedef struct { uint32_t magic_word; char version[32], project_name[32]; uint8_t app_elf_sha256[32]; } esp_app_desc_t;
typedef struct { size_t size; } esp_partition_t;
typedef struct mock_client *esp_http_client_handle_t;
typedef enum { HTTP_EVENT_ERROR, HTTP_EVENT_ON_CONNECTED, HTTP_EVENT_HEADERS_SENT, HTTP_EVENT_ON_HEADER,
               HTTP_EVENT_ON_HEADERS_COMPLETE, HTTP_EVENT_DISCONNECTED } esp_http_client_event_id_t;
typedef struct {
    esp_http_client_event_id_t event_id; esp_http_client_handle_t client;
    void *user_data; char *header_key, *header_value;
} esp_http_client_event_t;
typedef struct {
    const char *url; esp_err_t (*crt_bundle_attach)(void *);
    int timeout_ms, buffer_size, buffer_size_tx; bool keep_alive_enable;
    esp_err_t (*event_handler)(esp_http_client_event_t *); void *user_data;
} esp_http_client_config_t;
typedef void *esp_https_ota_handle_t;
typedef struct {
    const esp_http_client_config_t *http_config;
    esp_err_t (*http_client_init_cb)(esp_http_client_handle_t);
    uint32_t buffer_caps; bool ota_resumption; size_t ota_image_bytes_written;
    struct { const esp_partition_t *staging; } partition;
} esp_https_ota_config_t;
esp_err_t esp_crt_bundle_attach(void *);
int esp_http_client_get_status_code(esp_http_client_handle_t);
esp_err_t esp_http_client_get_and_clear_last_tls_error(esp_http_client_handle_t, int *, int *);
esp_err_t esp_http_client_get_user_data(esp_http_client_handle_t, void **);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t, const char *, const char *);
esp_err_t esp_https_ota_begin(const esp_https_ota_config_t *, esp_https_ota_handle_t *);
esp_err_t esp_https_ota_get_img_desc(esp_https_ota_handle_t, esp_app_desc_t *);
esp_err_t esp_https_ota_perform(esp_https_ota_handle_t);
esp_err_t esp_https_ota_finish(esp_https_ota_handle_t);
esp_err_t esp_https_ota_abort(esp_https_ota_handle_t);
int esp_https_ota_get_status_code(esp_https_ota_handle_t);
int esp_https_ota_get_image_size(esp_https_ota_handle_t);
int esp_https_ota_get_image_len_read(esp_https_ota_handle_t);
bool esp_https_ota_is_complete_data_received(esp_https_ota_handle_t);
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *);
const esp_app_desc_t *esp_app_get_description(void);
int64_t esp_timer_get_time(void);
void vTaskDelay(int);
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGE(tag, ...) ((void)(tag))
static inline const char *esp_err_to_name(esp_err_t e) { (void)e; return "test error"; }
static inline size_t heap_caps_get_free_size(int c) { (void)c; return 65536; }
static inline size_t heap_caps_get_largest_free_block(int c) { (void)c; return 32768; }
// 单线程主机 UI 回归;正式构建使用真实 FreeRTOS 临界区。
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
#define pdPASS 1
int xTaskCreate(void (*task)(void *), const char *, unsigned, void *, unsigned, void *);
void vTaskDelete(void *);
void esp_restart(void);
typedef enum { WIFI_PS_NONE, WIFI_PS_MIN_MODEM, WIFI_PS_MAX_MODEM } wifi_ps_type_t;
typedef struct { int unused; } wifi_ap_record_t;
esp_err_t esp_wifi_get_ps(wifi_ps_type_t *);
esp_err_t esp_wifi_set_ps(wifi_ps_type_t);
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *);
