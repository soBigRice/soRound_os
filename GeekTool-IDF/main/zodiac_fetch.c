#include "zodiac_data.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_wifi.h"
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
static void fetch(void *arg) {
    (void)arg;portENTER_CRITICAL(&s_lock);uint32_t generation=s_request;unsigned sign=s_sign;portEXIT_CRITICAL(&s_lock);
    char url[160];time_t now=time(NULL);struct tm day;localtime_r(&now,&day);
    snprintf(url,sizeof url,"https://v2.xxapi.cn/api/horoscope?type=%s&time=today&date=%04d%02d%02d",ZODIAC_KEYS[sign],day.tm_year+1900,day.tm_mon+1,day.tm_mday);
    esp_http_client_config_t cfg={.url=url,.crt_bundle_attach=esp_crt_bundle_attach,.timeout_ms=6000,
        .buffer_size=1024,.buffer_size_tx=512,.disable_auto_redirect=true};
    esp_http_client_handle_t client=NULL;char *body=NULL;zodiac_data_t *result=NULL;
    bool valid=false,opened=false;int status=0,total=0,read=-1;int64_t deadline=esp_timer_get_time()+8000000;
    if(!current(generation))goto done;
    client=esp_http_client_init(&cfg);body=malloc(ZODIAC_HTTP_BYTES);result=calloc(1,sizeof *result);
    if(!client||!body||!result)goto done;
    if(esp_http_client_set_header(client,"Accept","application/json")!=ESP_OK||
       esp_http_client_set_header(client,"Cache-Control","no-cache")!=ESP_OK||
       esp_http_client_set_header(client,"Accept-Encoding","identity")!=ESP_OK||esp_http_client_open(client,0)!=ESP_OK)goto done;
    opened=true;int64_t size=esp_http_client_fetch_headers(client);status=esp_http_client_get_status_code(client);
    if(size<0||size>=ZODIAC_HTTP_BYTES||status!=200)goto done;
    while(current(generation)&&esp_timer_get_time()<deadline&&total<ZODIAC_HTTP_BYTES-1) {
        read=esp_http_client_read(client,body+total,ZODIAC_HTTP_BYTES-1-total);if(read<=0)break;total+=read;
    }
    body[total]=0;valid=current(generation)&&read==0&&total>0&&total<ZODIAC_HTTP_BYTES-1&&
        esp_http_client_is_complete_data_received(client)&&zodiac_data_parse(body,(size_t)total,sign,result);
    if(valid&&day.tm_year+1900>=2024) {
        int month=0,date=0;valid=sscanf(result->date,"%d月%d日",&month,&date)==2&&month==day.tm_mon+1&&date==day.tm_mday;
    }
done:
    if(client){if(opened)esp_http_client_close(client);esp_http_client_cleanup(client);}free(body);
    portENTER_CRITICAL(&s_lock);if(generation==s_generation){if(valid)s_result=*result;s_state=valid?ZODIAC_READY:ZODIAC_FAILED;}
    s_alive=false;portEXIT_CRITICAL(&s_lock);free(result);ESP_LOGI("zodiac","fetch %s http=%d bytes=%d",valid?"ready":"failed",status,total);vTaskDelete(NULL);
}
zodiac_state_t zodiac_fetch_begin(unsigned sign,uint32_t *token) {
    if(!token||sign>=ZODIAC_COUNT)return ZODIAC_FAILED;
    wifi_ap_record_t ap;if(esp_wifi_sta_get_ap_info(&ap)!=ESP_OK)return ZODIAC_OFFLINE;
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
