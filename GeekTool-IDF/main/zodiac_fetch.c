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
    if(body && result)fetched=network_http_get("zodiac",&cfg,body,ZODIAC_HTTP_BYTES,8000,request_active,&generation);
    else ESP_LOGW("zodiac","response allocation failed");
    if(fetched.stage==NET_HTTP_OK) {
        valid=zodiac_data_parse(body,fetched.bytes,sign,result);
        if(valid && day.tm_year+1900>=2024) {
            int month=0,date=0;valid=sscanf(result->date,"%d月%d日",&month,&date)==2 && month==day.tm_mon+1 && date==day.tm_mday;
        }
        if(!valid)ESP_LOGW("zodiac","response sign/date/parse failed HTTP=%d bytes=%u",fetched.status,(unsigned)fetched.bytes);
    }
    free(body);
    portENTER_CRITICAL(&s_lock);if(generation==s_generation){if(valid)s_result=*result;s_state=valid?ZODIAC_READY:ZODIAC_FAILED;}
    s_alive=false;portEXIT_CRITICAL(&s_lock);free(result);ESP_LOGI("zodiac","fetch %s http=%d bytes=%d",valid?"ready":"failed",fetched.status,(int)fetched.bytes);vTaskDelete(NULL);
}
zodiac_state_t zodiac_fetch_begin(unsigned sign,uint32_t *token) {
    if(!token||sign>=ZODIAC_COUNT)return ZODIAC_FAILED;
    if(!wifi_service_ready())return ZODIAC_OFFLINE;
    portENTER_CRITICAL(&s_lock);if(s_alive){portEXIT_CRITICAL(&s_lock);return ZODIAC_BUSY;}
    *token=++s_generation;s_request=s_generation;s_sign=sign;s_state=ZODIAC_LOADING;s_alive=true;portEXIT_CRITICAL(&s_lock);
    if(xTaskCreate(fetch,"zodiac",8192,NULL,5,NULL)!=pdPASS){portENTER_CRITICAL(&s_lock);s_alive=false;s_state=ZODIAC_FAILED;portEXIT_CRITICAL(&s_lock);return ZODIAC_FAILED;}
    return ZODIAC_LOADING;
}
zodiac_state_t zodiac_fetch_poll(uint32_t token,zodiac_data_t *out) {
    portENTER_CRITICAL(&s_lock);zodiac_state_t state=token==s_generation?s_state:ZODIAC_IDLE;
    if(state==ZODIAC_READY&&out)*out=s_result;
    portEXIT_CRITICAL(&s_lock);return state;
}
void zodiac_fetch_cancel(void){portENTER_CRITICAL(&s_lock);++s_generation;s_state=ZODIAC_IDLE;portEXIT_CRITICAL(&s_lock);}
