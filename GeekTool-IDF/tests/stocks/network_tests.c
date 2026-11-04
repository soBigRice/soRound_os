#include "fixture.h"
#include "stocks_fetch.h"
#include "network_http.h"
#include "esp_crt_bundle.h"
#include "freertos/task.h"
#include <string.h>

static bool online=true,create_fail;
static unsigned created,deleted,calls,progress_checks;
static int fail_index=-1,bad_index=-1,cancel_index=-1,offline_index=-1;
static int64_t clock_us,advance_us;
static uint32_t current_token;
static stock_list_t expected;
static void (*worker)(void *);
bool wifi_service_ready(void){return online;}
int64_t esp_timer_get_time(void){return clock_us;}
esp_err_t esp_crt_bundle_attach(void *arg){(void)arg;return ESP_OK;}
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle) {
    assert(!strcmp(name,"stocks")&&stack==8192&&!arg&&priority==5&&!handle&&!worker);
    if(create_fail)return 0;
    worker=fn;++created;return pdPASS;
}
void vTaskDelete(void *arg){assert(!arg);++deleted;}
network_http_result_t network_http_get(const char *tag,const esp_http_client_config_t *cfg,char *body,size_t cap,int budget,network_http_active_fn active,void *arg) {
    assert(!strcmp(tag,"stocks")&&cap==STOCK_HTTP_BYTES&&budget>0&&budget<=8000);
    assert(cfg->timeout_ms>0&&cfg->timeout_ms<=6000&&cfg->disable_auto_redirect&&cfg->crt_bundle_attach==esp_crt_bundle_attach);
    if(!active(arg))return (network_http_result_t){.stage=NET_HTTP_CANCELED};
    unsigned index=calls++;
    assert(index<expected.count);
    char url[160];snprintf(url,sizeof url,"https://web.ifzq.gtimg.cn/appstock/app/minute/query?code=%s",expected.symbols[index]);
    assert(!strcmp(cfg->url,url));
    stock_batch_t progress;
    assert(stocks_fetch_poll(current_token,&progress)&&!progress.complete&&progress.list.count==expected.count);
    if(index) {
        assert(progress.states[index-1]!=(stock_fetch_state_t)STOCK_LOADING);
        if(progress.states[index-1]==STOCK_READY)assert(!strcmp(progress.quotes[index-1].symbol,expected.symbols[index-1]));
        assert(progress.states[index]==STOCK_LOADING);++progress_checks;
    }
    if((int)index==cancel_index){stocks_fetch_cancel();assert(!active(arg));return (network_http_result_t){.stage=NET_HTTP_CANCELED};}
    size_t n=stock_fixture(expected.symbols[index],body);
    if((int)index==bad_index){strcpy(body,"{}");n=2;}
    if((int)index==offline_index)online=false;
    clock_us+=advance_us;
    return (network_http_result_t){.stage=(int)index==fail_index?NET_HTTP_READ:NET_HTTP_OK,.status=200,.bytes=n};
}
static void run(void) {
    assert(worker);void (*fn)(void *)=worker;worker=NULL;fn(NULL);assert(created==deleted);
}
static void reset(void) {
    calls=0;fail_index=bad_index=cancel_index=offline_index=-1;advance_us=clock_us=0;online=true;
}
static void begin(void){assert(stocks_fetch_begin(&expected,&current_token)==STOCK_LOADING);}
int main(void) {
    const char *symbols[]={"sh600519","sz000001","hk00700","usAAPL","hk80700","sh000001","sz399001","sh601318"};
    expected.count=8;for(unsigned i=0;i<8;++i)strcpy(expected.symbols[i],symbols[i]);
    uint32_t token;stock_batch_t result;
    online=false;assert(stocks_fetch_begin(&expected,&token)==STOCK_OFFLINE&&!worker);online=true;
    stock_list_t invalid=expected;strcpy(invalid.symbols[0],"sh600519&x=1");assert(stocks_fetch_begin(&invalid,&token)==STOCK_FAILED&&!worker);
    invalid=expected;strcpy(invalid.symbols[1],invalid.symbols[0]);assert(stocks_fetch_begin(&invalid,&token)==STOCK_FAILED);
    invalid=expected;invalid.count=9;assert(stocks_fetch_begin(&invalid,&token)==STOCK_FAILED);
    invalid.count=0;assert(stocks_fetch_begin(&invalid,&token)==STOCK_FAILED);
    create_fail=true;assert(stocks_fetch_begin(&expected,&token)==STOCK_FAILED&&!worker);create_fail=false;
    reset();begin();uint32_t first=current_token;
    assert(stocks_fetch_begin(&expected,&token)==STOCK_BUSY);
    run();assert(calls==8&&stocks_fetch_poll(first,&result)&&result.complete);
    for(unsigned i=0;i<8;++i)assert(result.states[i]==STOCK_READY&&!strcmp(result.quotes[i].symbol,symbols[i]));
    assert(!strcmp(result.quotes[4].currency,"CNY")&&progress_checks>=7);
    reset();fail_index=1;bad_index=4;begin();run();assert(calls==8&&stocks_fetch_poll(current_token,&result)&&result.complete);
    for(unsigned i=0;i<8;++i)assert(result.states[i]==(i==1||i==4?STOCK_FAILED:STOCK_READY));
    reset();begin();stocks_fetch_cancel();run();assert(!calls&&!stocks_fetch_poll(current_token,&result));
    reset();cancel_index=2;begin();run();assert(calls==3&&!stocks_fetch_poll(current_token,&result));
    reset();offline_index=0;begin();run();assert(calls==1&&stocks_fetch_poll(current_token,&result)&&result.complete&&result.states[0]==STOCK_READY);
    for(unsigned i=1;i<8;++i)assert(result.states[i]==STOCK_OFFLINE);
    reset();advance_us=8000000;begin();run();assert(calls==3&&stocks_fetch_poll(current_token,&result)&&result.complete);
    for(unsigned i=0;i<8;++i)assert(result.states[i]==(i<3?STOCK_READY:STOCK_FAILED));
    assert(!stocks_fetch_poll(current_token,NULL));
    puts("Full eight-symbol refresh, incremental results, partial failure, cancellation and budget passed");
    return 0;
}
