#include "stocks_fetch.h"
#include "network_http.h"
#include "wifi_service.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BATCH_BUDGET_US (24LL*1000*1000)
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static uint32_t generation,request;
static bool alive;
static stock_batch_t batch;

static bool active(void *arg) {
    portENTER_CRITICAL(&mux);
    bool current=*(const uint32_t *)arg==generation;
    portEXIT_CRITICAL(&mux);
    return current;
}
static void publish(uint32_t ticket,unsigned index,stock_fetch_state_t state,const stock_quote_t *quote) {
    portENTER_CRITICAL(&mux);
    if(ticket==generation) {
        if(quote)batch.quotes[index]=*quote;
        batch.states[index]=state;
        ++batch.revision;
    }
    portEXIT_CRITICAL(&mux);
}
static void fetch(void *arg) {
    (void)arg;
    stock_list_t list;
    portENTER_CRITICAL(&mux);
    uint32_t ticket=request;
    list=batch.list;
    portEXIT_CRITICAL(&mux);
    int64_t deadline=esp_timer_get_time()+BATCH_BUDGET_US;
    // 复用PSRAM优先的正文缓冲，单worker避免并发TLS挤占内部堆。
    // Reuse one PSRAM-preferred response buffer; a single worker bounds TLS heap pressure.
    char *body=malloc(STOCK_HTTP_BYTES);
    for(unsigned i=0;i<list.count;++i) {
        if(!active(&ticket))break;
        int64_t left=(deadline-esp_timer_get_time())/1000;
        bool online=wifi_service_ready();
        if(!body || !online || left<=0) {
            for(unsigned j=i;j<list.count;++j)publish(ticket,j,online?STOCK_FAILED:STOCK_OFFLINE,NULL);
            break;
        }
        char url[160];
        snprintf(url,sizeof url,"https://web.ifzq.gtimg.cn/appstock/app/minute/query?code=%s",list.symbols[i]);
        int budget=(int)(left<8000?left:8000);
        esp_http_client_config_t config={.url=url,.crt_bundle_attach=esp_crt_bundle_attach,
            .timeout_ms=budget<6000?budget:6000,.buffer_size=1024,.buffer_size_tx=512,.disable_auto_redirect=true};
        network_http_result_t fetched=network_http_get("stocks",&config,body,STOCK_HTTP_BYTES,budget,active,&ticket);
        stock_quote_t quote={0};
        bool valid=fetched.stage==NET_HTTP_OK && stock_quote_parse(body,fetched.bytes,list.symbols[i],&quote);
        if(!active(&ticket))break;
        stock_fetch_state_t state=valid?STOCK_READY:fetched.stage==NET_HTTP_OFFLINE?STOCK_OFFLINE:STOCK_FAILED;
        publish(ticket,i,state,valid?&quote:NULL);
    }
    free(body);
    portENTER_CRITICAL(&mux);
    if(ticket==generation) {
        batch.complete=true;
        ++batch.revision;
    }
    alive=false;
    portEXIT_CRITICAL(&mux);
    // 数量可用于诊断，不输出自选代码或正文。/ Log counts without watchlist symbols or bodies.
    ESP_LOGI("stocks","watchlist worker finished total=%u",list.count);
    vTaskDelete(NULL);
}
stock_fetch_state_t stocks_fetch_begin(const stock_list_t *list,uint32_t *token) {
    if(!list || !token || !list->count || list->count>STOCK_LIST_MAX)return STOCK_FAILED;
    stock_list_t normalized={.count=list->count};
    for(unsigned i=0;i<list->count;++i) {
        if(!memchr(list->symbols[i],0,STOCK_SYMBOL_BYTES) || !stock_symbol_normalize(list->symbols[i],normalized.symbols[i]))return STOCK_FAILED;
        for(unsigned j=0;j<i;++j)if(!strcmp(normalized.symbols[i],normalized.symbols[j]))return STOCK_FAILED;
    }
    if(!wifi_service_ready())return STOCK_OFFLINE;
    portENTER_CRITICAL(&mux);
    if(alive) {
        portEXIT_CRITICAL(&mux);
        return STOCK_BUSY;
    }
    *token=++generation;
    request=generation;
    batch=(stock_batch_t){.list=normalized,.revision=1};
    for(unsigned i=0;i<list->count;++i)batch.states[i]=STOCK_LOADING;
    alive=true;
    portEXIT_CRITICAL(&mux);
    if(xTaskCreate(fetch,"stocks",8192,NULL,5,NULL)!=pdPASS) {
        portENTER_CRITICAL(&mux);
        alive=false;
        portEXIT_CRITICAL(&mux);
        return STOCK_FAILED;
    }
    return STOCK_LOADING;
}
bool stocks_fetch_poll(uint32_t token,stock_batch_t *out) {
    if(!out)return false;
    portENTER_CRITICAL(&mux);
    bool current=token==generation;
    if(current)*out=batch;
    portEXIT_CRITICAL(&mux);
    return current;
}
void stocks_fetch_cancel(void) {
    portENTER_CRITICAL(&mux);
    ++generation;
    portEXIT_CRITICAL(&mux);
}
