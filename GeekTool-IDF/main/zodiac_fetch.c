#include "zodiac_data.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "wifi_service.h"
#include "network_http.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
static portMUX_TYPE s_lock=portMUX_INITIALIZER_UNLOCKED;
static bool s_alive;
static uint32_t s_generation,s_request;
static unsigned s_sign;
static zodiac_state_t s_state;
static zodiac_data_t s_result;
static zodiac_failure_t s_failure;
static bool current(uint32_t generation){portENTER_CRITICAL(&s_lock);bool result=generation==s_generation;portEXIT_CRITICAL(&s_lock);return result;}
static bool request_active(void *arg){return current(*(const uint32_t *)arg);}
static void fetch(void *arg) {
    (void)arg;portENTER_CRITICAL(&s_lock);uint32_t generation=s_request;unsigned sign=s_sign;portEXIT_CRITICAL(&s_lock);
    char url[160];time_t now=time(NULL);struct tm day;localtime_r(&now,&day);
    snprintf(url,sizeof url,"https://v2.xxapi.cn/api/horoscope?type=%s&time=today&date=%04d%02d%02d",ZODIAC_KEYS[sign],day.tm_year+1900,day.tm_mon+1,day.tm_mday);
    esp_http_client_config_t cfg={.url=url,.crt_bundle_attach=esp_crt_bundle_attach,.timeout_ms=6000,
        .buffer_size=1024,.buffer_size_tx=512,.disable_auto_redirect=true};
    char *body=malloc(ZODIAC_HTTP_BYTES);zodiac_data_t *result=calloc(1,sizeof *result);
    bool valid=false;network_http_result_t fetched={.stage=NET_HTTP_INIT};
    zodiac_failure_t failure={.reason=ZODIAC_ERROR_MEMORY,
        .requested_day=(day.tm_year+1900)*10000+(day.tm_mon+1)*100+day.tm_mday};
    if(body && result)fetched=network_http_get("zodiac",&cfg,body,ZODIAC_HTTP_BYTES,8000,request_active,&generation);
    else ESP_LOGW("zodiac","response allocation failed");
    if(fetched.stage==NET_HTTP_OK) {
        valid=zodiac_data_parse(body,fetched.bytes,sign,result);
        failure.reason=valid?ZODIAC_ERROR_NONE:ZODIAC_ERROR_RESPONSE;
        if(valid && day.tm_year+1900>=2024) {
            int month=0,date=0;valid=sscanf(result->date,"%d月%d日",&month,&date)==2 && month==day.tm_mon+1 && date==day.tm_mday;
            if(!valid)failure.reason=ZODIAC_ERROR_DATE;
        }
    }else if(body && result) {
        // 离线/取消时SDK还未分配客户端，不能采用初始化的NO_MEM占位错误。
        // Offline/canceled preflight precedes allocation; ignore its initial NO_MEM placeholder.
        failure.reason=fetched.stage==NET_HTTP_OFFLINE||fetched.stage==NET_HTTP_CANCELED?ZODIAC_ERROR_CONNECTION:
            fetched.error==ESP_ERR_NO_MEM?ZODIAC_ERROR_MEMORY:
            fetched.stage==NET_HTTP_SIZE||fetched.stage==NET_HTTP_INCOMPLETE?ZODIAC_ERROR_RESPONSE:
            fetched.stage==NET_HTTP_TIMEOUT?ZODIAC_ERROR_TIMEOUT:
            fetched.tls_flags||fetched.tls_code?ZODIAC_ERROR_TLS:
            fetched.stage==NET_HTTP_STATUS?ZODIAC_ERROR_HTTP:ZODIAC_ERROR_CONNECTION;
    }
    failure.http_status=fetched.status;
    if(result)snprintf(failure.response_date,sizeof failure.response_date,"%s",result->date);
    // 网络200不等于当天内容可用；保留请求日/返回日以区分校时与内容故障。
    // HTTP 200 does not prove today's data: keep both dates for clock/provider diagnosis.
    if(!valid)ESP_LOGW("zodiac","failure sign=%s requested=%08d response=%s reason=%d stage=%d HTTP=%d err=%s TLS=%d flags=0x%x bytes=%u",
        ZODIAC_KEYS[sign],failure.requested_day,failure.response_date,(int)failure.reason,(int)fetched.stage,
        fetched.status,esp_err_to_name(fetched.error),fetched.tls_code,fetched.tls_flags,(unsigned)fetched.bytes);
    free(body);
    portENTER_CRITICAL(&s_lock);if(generation==s_generation){if(valid)s_result=*result;s_failure=failure;s_state=valid?ZODIAC_READY:ZODIAC_FAILED;}
    s_alive=false;portEXIT_CRITICAL(&s_lock);free(result);
    ESP_LOGI("zodiac","fetch %s sign=%s requested=%08d response=%s http=%d bytes=%u",
        valid?"ready":"failed",ZODIAC_KEYS[sign],failure.requested_day,failure.response_date,fetched.status,(unsigned)fetched.bytes);vTaskDelete(NULL);
}
zodiac_state_t zodiac_fetch_begin(unsigned sign,uint32_t *token) {
    if(!token||sign>=ZODIAC_COUNT)return ZODIAC_FAILED;
    if(!wifi_service_ready())return ZODIAC_OFFLINE;
    portENTER_CRITICAL(&s_lock);if(s_alive){portEXIT_CRITICAL(&s_lock);return ZODIAC_BUSY;}
    *token=++s_generation;s_request=s_generation;s_sign=sign;s_state=ZODIAC_LOADING;s_alive=true;
    s_failure=(zodiac_failure_t){0};portEXIT_CRITICAL(&s_lock);
    if(xTaskCreate(fetch,"zodiac",8192,NULL,5,NULL)!=pdPASS){portENTER_CRITICAL(&s_lock);s_alive=false;s_state=ZODIAC_FAILED;s_failure.reason=ZODIAC_ERROR_MEMORY;portEXIT_CRITICAL(&s_lock);return ZODIAC_FAILED;}
    return ZODIAC_LOADING;
}
bool zodiac_fetch_failure(uint32_t token,zodiac_failure_t *out) {
    if(!out)return false;
    portENTER_CRITICAL(&s_lock);bool valid=token==s_generation&&s_state==ZODIAC_FAILED;
    if(valid)*out=s_failure;
    portEXIT_CRITICAL(&s_lock);return valid;
}
zodiac_state_t zodiac_fetch_poll(uint32_t token,zodiac_data_t *out) {
    portENTER_CRITICAL(&s_lock);zodiac_state_t state=token==s_generation?s_state:ZODIAC_IDLE;
    if(state==ZODIAC_READY&&out)*out=s_result;
    portEXIT_CRITICAL(&s_lock);return state;
}
void zodiac_fetch_cancel(void){portENTER_CRITICAL(&s_lock);++s_generation;s_state=ZODIAC_IDLE;portEXIT_CRITICAL(&s_lock);}
