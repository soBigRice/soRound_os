#include "answers_data.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "wifi_service.h"
#include "network_http.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <stdio.h>

static portMUX_TYPE s_lock=portMUX_INITIALIZER_UNLOCKED;
static bool s_alive;
static uint32_t s_generation, s_request;
static answer_fetch_state_t s_state;
static answer_response_t s_response;

static bool current(uint32_t generation) {
    portENTER_CRITICAL(&s_lock);bool active=generation==s_generation;
    portEXIT_CRITICAL(&s_lock);return active;
}

static bool request_active(void *arg){return current(*(const uint32_t *)arg);}

static void fetch_task(void *arg) {
    (void)arg;
    portENTER_CRITICAL(&s_lock);uint32_t generation=s_request;portEXIT_CRITICAL(&s_lock);
    // Identical questions return the same response even with no-cache. Use a fresh, non-private nonce.
    char url[128];
    snprintf(url,sizeof url,"%s%%20%08lx%08lx",ANSWER_API_URL,
             (unsigned long)esp_random(),(unsigned long)esp_random());
    esp_http_client_config_t cfg={.url=url,.crt_bundle_attach=esp_crt_bundle_attach,
        .timeout_ms=6000,.buffer_size=1024,.buffer_size_tx=512,.disable_auto_redirect=true};
    char *body=malloc(ANSWER_HTTP_BYTES);
    answer_response_t result={0};bool valid=false;
    network_http_result_t fetched={.stage=NET_HTTP_INIT};
    if(body)fetched=network_http_get("answers",&cfg,body,ANSWER_HTTP_BYTES,8000,request_active,&generation);
    else ESP_LOGW("answers","response allocation failed");
    if(fetched.stage==NET_HTTP_OK) {
        valid=answers_data_parse(body,fetched.bytes,&result);
        if(!valid)ESP_LOGW("answers","response parse failed HTTP=%d bytes=%u",fetched.status,(unsigned)fetched.bytes);
    }
    free(body);
    portENTER_CRITICAL(&s_lock);
    if(generation==s_generation) {
        if(valid)s_response=result;
        s_state=valid?ANSWER_FETCH_READY:ANSWER_FETCH_FAILED;
    }
    s_alive=false;portEXIT_CRITICAL(&s_lock);
    ESP_LOGI("answers","fetch %s http=%d bytes=%d",valid?"ready":"failed",fetched.status,(int)fetched.bytes);
    vTaskDelete(NULL);
}

answer_fetch_state_t answers_fetch_begin(uint32_t *token) {
    if(!token)return ANSWER_FETCH_FAILED;
    if(!wifi_service_ready())return ANSWER_FETCH_OFFLINE;
    portENTER_CRITICAL(&s_lock);
    if(s_alive){portEXIT_CRITICAL(&s_lock);return ANSWER_FETCH_BUSY;}
    ++s_generation;s_request=s_generation;*token=s_request;
    s_state=ANSWER_FETCH_LOADING;s_alive=true;portEXIT_CRITICAL(&s_lock);
    if(xTaskCreate(fetch_task,"answers",8192,NULL,5,NULL)!=pdPASS) {
        portENTER_CRITICAL(&s_lock);s_alive=false;s_state=ANSWER_FETCH_FAILED;portEXIT_CRITICAL(&s_lock);
        return ANSWER_FETCH_FAILED;
    }
    return ANSWER_FETCH_LOADING;
}

answer_fetch_state_t answers_fetch_poll(uint32_t token,answer_response_t *out) {
    portENTER_CRITICAL(&s_lock);
    answer_fetch_state_t state=token==s_generation?s_state:ANSWER_FETCH_IDLE;
    if(state==ANSWER_FETCH_READY && out)*out=s_response;
    portEXIT_CRITICAL(&s_lock);return state;
}

void answers_fetch_cancel(void) {
    // Never delete a task inside SDK I/O: its owner closes HTTP and frees TLS/body allocations.
    portENTER_CRITICAL(&s_lock);++s_generation;s_state=ANSWER_FETCH_IDLE;portEXIT_CRITICAL(&s_lock);
}
