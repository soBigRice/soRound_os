#include "startup_network.h"
#include "ota_update.h"
#include "wifi_service.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static portMUX_TYPE s_lock=portMUX_INITIALIZER_UNLOCKED;
static bool s_alive,s_cancel;
static const char *s_url;
static startup_network_result_t s_result;
static bool canceled(void){portENTER_CRITICAL(&s_lock);bool cancel=s_cancel;portEXIT_CRITICAL(&s_lock);return cancel;}
static void probe(void *arg) {
    (void)arg;
    startup_network_result_t result={.state=STARTUP_NET_FAILED,.error=ESP_ERR_NO_MEM};
    esp_http_client_handle_t client=NULL;bool opened=false;
    if(canceled()){result.state=STARTUP_NET_TIMEOUT;result.error=ESP_ERR_TIMEOUT;goto done;}
    if(!wifi_service_ready()){result.state=STARTUP_NET_OFFLINE;result.error=ESP_OK;goto done;}
    esp_http_client_config_t config={.url=s_url,.method=HTTP_METHOD_HEAD,.timeout_ms=3500,
        .crt_bundle_attach=esp_crt_bundle_attach,.disable_auto_redirect=true,.buffer_size=1024,.buffer_size_tx=512};
    client=esp_http_client_init(&config);if(!client)goto done;
    result.error=esp_http_client_set_header(client,"Cache-Control","no-cache");
    if(result.error!=ESP_OK)goto cleanup;
    result.error=esp_http_client_open(client,0);if(result.error!=ESP_OK)goto cleanup;
    opened=true;
    if(canceled()){result.state=STARTUP_NET_TIMEOUT;result.error=ESP_ERR_TIMEOUT;goto cleanup;}
    int64_t headers=esp_http_client_fetch_headers(client);
    result.http_status=esp_http_client_get_status_code(client);
    if(headers>=0 && result.http_status==200){result.state=STARTUP_NET_READY;result.error=ESP_OK;}
    else result.error=ESP_FAIL;
cleanup:
    if(result.state!=STARTUP_NET_READY) {
        result.socket_errno=esp_http_client_get_errno(client);
        result.transport_error=esp_http_client_get_and_clear_last_tls_error(client,&result.tls_code,&result.tls_flags);
    }
    if(opened)esp_http_client_close(client);
    esp_http_client_cleanup(client);
done:
    portENTER_CRITICAL(&s_lock);
    if(!s_cancel)s_result=result;
    s_alive=false;portEXIT_CRITICAL(&s_lock);
    ESP_LOGI("boot-net","state=%d err=%s transport=%s errno=%d HTTP=%d TLS=%d flags=0x%x",result.state,
        esp_err_to_name(result.error),esp_err_to_name(result.transport_error),result.socket_errno,result.http_status,result.tls_code,result.tls_flags);
    vTaskDelete(NULL);
}
startup_network_result_t startup_network_check(bool beta) {
    startup_network_result_t waiting={.state=STARTUP_NET_TIMEOUT,.error=ESP_ERR_TIMEOUT};
    if(!wifi_service_ready())return (startup_network_result_t){.state=STARTUP_NET_OFFLINE,.error=ESP_OK};
    portENTER_CRITICAL(&s_lock);
    if(s_alive){portEXIT_CRITICAL(&s_lock);return waiting;}
    s_alive=true;s_cancel=false;s_url=beta?OTA_URL_BETA:OTA_URL_STABLE;s_result=waiting;
    portEXIT_CRITICAL(&s_lock);
    if(xTaskCreate(probe,"boot_net",8192,NULL,5,NULL)!=pdPASS) {
        portENTER_CRITICAL(&s_lock);s_alive=false;portEXIT_CRITICAL(&s_lock);
        return (startup_network_result_t){.state=STARTUP_NET_FAILED,.error=ESP_ERR_NO_MEM};
    }
    int64_t deadline=esp_timer_get_time()+8500000;
    for(;;) {
        portENTER_CRITICAL(&s_lock);bool alive=s_alive;startup_network_result_t result=s_result;portEXIT_CRITICAL(&s_lock);
        if(!alive)return result;
        if(esp_timer_get_time()>=deadline) {
            portENTER_CRITICAL(&s_lock);s_cancel=true;portEXIT_CRITICAL(&s_lock);
            ESP_LOGW("boot-net","probe timed out; worker will release its SDK resources at the next I/O boundary");
            return waiting;
        }
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}
