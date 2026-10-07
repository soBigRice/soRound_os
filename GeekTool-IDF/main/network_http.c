#include "network_http.h"
#include "wifi_service.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <limits.h>

static unsigned magnitude(int code){return code<0?0u-(unsigned)code:(unsigned)code;}
static bool retryable(const network_http_result_t *r) {
    if(r->tls_flags || r->error==ESP_ERR_NO_MEM ||
       magnitude(r->tls_code)==magnitude(MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) ||
       magnitude(r->tls_code)==magnitude(MBEDTLS_ERR_X509_FATAL_ERROR) ||
       magnitude(r->tls_code)==magnitude(MBEDTLS_ERR_SSL_ALLOC_FAILED) ||
       magnitude(r->tls_code)==magnitude(MBEDTLS_ERR_X509_ALLOC_FAILED))return false;
    if(r->stage==NET_HTTP_STATUS)return r->status==408 || (r->status>=500 && r->status<600);
    return r->stage==NET_HTTP_CONNECT || r->stage==NET_HTTP_HEADERS ||
        r->stage==NET_HTTP_READ || r->stage==NET_HTTP_INCOMPLETE;
}
static bool still_active(network_http_active_fn active,void *user){return !active || active(user);}
static bool timeout(esp_http_client_handle_t client,int64_t deadline,int limit) {
    int64_t left=(deadline-esp_timer_get_time())/1000;
    return left>0 && esp_http_client_set_timeout_ms(client,(int)(left<limit?left:limit))==ESP_OK;
}

network_http_result_t network_http_get(const char *tag,const esp_http_client_config_t *config,
    char *body,size_t capacity,int budget_ms,network_http_active_fn active,void *user) {
    network_http_result_t r={.stage=NET_HTTP_INIT,.error=ESP_ERR_INVALID_ARG};
    if(!tag || !config || !body || capacity<2 || capacity>INT_MAX || budget_ms<=0 || config->timeout_ms<=0)return r;
    int64_t deadline=esp_timer_get_time()+(int64_t)budget_ms*1000;
    for(int attempt=1;attempt<=2;++attempt) {
        r=(network_http_result_t){.stage=NET_HTTP_INIT,.error=ESP_ERR_NO_MEM,.attempt=attempt};
        body[0]=0;
        if(!still_active(active,user)){r.stage=NET_HTTP_CANCELED;return r;}
        if(!wifi_service_ready()){r.stage=NET_HTTP_OFFLINE;return r;}
        esp_http_client_handle_t client=esp_http_client_init(config);
        if(!client){ESP_LOGW(tag,"HTTP client allocation failed");break;}
        bool opened=false;
        r.error=esp_http_client_set_header(client,"Accept","application/json");
        if(r.error==ESP_OK)r.error=esp_http_client_set_header(client,"Cache-Control","no-cache");
        if(r.error==ESP_OK)r.error=esp_http_client_set_header(client,"Accept-Encoding","identity");
        if(r.error!=ESP_OK)goto done;
        r.stage=NET_HTTP_TIMEOUT;r.error=ESP_ERR_TIMEOUT;
        if(!timeout(client,deadline,config->timeout_ms))goto done;
        r.stage=NET_HTTP_CONNECT;r.error=esp_http_client_open(client,0);
        if(r.error!=ESP_OK)goto done;
        opened=true;
        if(!still_active(active,user)){r.stage=NET_HTTP_CANCELED;goto done;}
        r.stage=NET_HTTP_TIMEOUT;r.error=ESP_ERR_TIMEOUT;
        if(!timeout(client,deadline,config->timeout_ms))goto done;
        int64_t size=esp_http_client_fetch_headers(client);
        r.status=esp_http_client_get_status_code(client);
        r.stage=NET_HTTP_HEADERS;r.error=ESP_FAIL;
        if(size<0)goto done;
        r.stage=NET_HTTP_STATUS;
        if(r.status!=200)goto done;
        r.stage=NET_HTTP_SIZE;
        if(size>=(int64_t)capacity)goto done;
        while(r.bytes<capacity-1) {
            if(!still_active(active,user)){r.stage=NET_HTTP_CANCELED;goto done;}
            r.stage=NET_HTTP_TIMEOUT;r.error=ESP_ERR_TIMEOUT;
            if(!timeout(client,deadline,config->timeout_ms))goto done;
            size_t left=capacity-1-r.bytes;
            // A full-body read can perform many blocking transport reads inside
            // IDF. Bound each call so cancellation/budget checks run between chunks.
            int n=esp_http_client_read(client,body+r.bytes,(int)(left<1024?left:1024));
            r.stage=NET_HTTP_READ;r.error=ESP_FAIL;
            if(n<0)goto done;
            if(!n)break;
            r.bytes+=(size_t)n;
        }
        body[r.bytes]=0;
        if(!still_active(active,user)){r.stage=NET_HTTP_CANCELED;goto done;}
        r.stage=NET_HTTP_TIMEOUT;r.error=ESP_ERR_TIMEOUT;
        if(esp_timer_get_time()>=deadline)goto done;
        r.stage=r.bytes>=capacity-1?NET_HTTP_SIZE:NET_HTTP_INCOMPLETE;r.error=ESP_FAIL;
        if(!r.bytes || r.bytes>=capacity-1 || !esp_http_client_is_complete_data_received(client))goto done;
        r.stage=NET_HTTP_OK;r.error=ESP_OK;
done:
        // Capture DNS/TCP/backend TLS details before close destroys the transport.
        if(r.stage!=NET_HTTP_OK) {
            r.socket_errno=esp_http_client_get_errno(client);
            r.transport_error=esp_http_client_get_and_clear_last_tls_error(client,&r.tls_code,&r.tls_flags);
        }
        if(opened)esp_http_client_close(client);
        esp_http_client_cleanup(client);
        if(r.stage==NET_HTTP_OK || r.stage==NET_HTTP_CANCELED)break;
        ESP_LOGW(tag,"request stage=%d attempt=%d err=%s transport=%s errno=%d HTTP=%d TLS=%d flags=0x%x bytes=%u internal=%u largest=%u psram=%u",
            r.stage,r.attempt,esp_err_to_name(r.error),esp_err_to_name(r.transport_error),r.socket_errno,
            r.status,r.tls_code,r.tls_flags,(unsigned)r.bytes,
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        if(!retryable(&r) || attempt==2 || !still_active(active,user) || !wifi_service_ready() ||
           deadline-esp_timer_get_time()<=200000)break;
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    return r;
}
