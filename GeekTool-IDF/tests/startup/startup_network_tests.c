#include "startup_network.h"
#include "answers_test_sdk.h"
#include "ota_update.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct mock_client {int unused;} client;
static bool online=true,hold,create_fail,init_fail,beta;
static int64_t now,headers=4100000;
static esp_err_t open_error,transport;
static int status=200,tls,flags,socket_error,handles,closes,cleanups,creates,deletes;
static void (*queued)(void *);
bool wifi_service_ready(void){return online;}
esp_err_t esp_crt_bundle_attach(void *ctx){(void)ctx;return ESP_OK;}
int64_t esp_timer_get_time(void){return now;}
int xTaskCreate(void (*task)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle){
    assert(!strcmp(name,"boot_net") && stack==8192 && !arg && priority==5 && !handle && !queued);++creates;
    if(create_fail)return 0;queued=task;return pdPASS;
}
void vTaskDelete(void *task){assert(!task);++deletes;}
static void run(void){assert(queued);void (*task)(void *)=queued;queued=NULL;task(NULL);assert(!handles);}
void vTaskDelay(int ms){assert(ms==25);now+=(int64_t)ms*1000;if(queued && !hold)run();}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg){
    assert(!strcmp(cfg->url,beta?OTA_URL_BETA:OTA_URL_STABLE) && cfg->method==HTTP_METHOD_HEAD);
    assert(cfg->crt_bundle_attach==esp_crt_bundle_attach && cfg->disable_auto_redirect && cfg->timeout_ms==3500);
    if(init_fail)return NULL;assert(!handles);++handles;return &client;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c,const char *key,const char *value){assert(c==&client && !strcmp(key,"Cache-Control") && !strcmp(value,"no-cache"));return ESP_OK;}
esp_err_t esp_http_client_open(esp_http_client_handle_t c,int size){assert(c==&client && !size);return open_error;}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c){assert(c==&client);return headers;}
int esp_http_client_get_status_code(esp_http_client_handle_t c){assert(c==&client);return status;}
int esp_http_client_get_errno(esp_http_client_handle_t c){assert(c==&client);return socket_error;}
esp_err_t esp_http_client_get_and_clear_last_tls_error(esp_http_client_handle_t c,int *code,int *f){assert(c==&client);*code=tls;*f=flags;return transport;}
esp_err_t esp_http_client_close(esp_http_client_handle_t c){assert(c==&client);++closes;return ESP_OK;}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c){assert(c==&client && handles==1);--handles;++cleanups;tls=flags=socket_error=transport=0;return ESP_OK;}
int esp_http_client_read(esp_http_client_handle_t c,char *body,int bytes){(void)c;(void)body;(void)bytes;assert(!"HEAD must not read firmware body");return -1;}
static startup_network_result_t check(void){now=0;startup_network_result_t r=startup_network_check(beta);if(!hold)assert(!handles && !queued);return r;}
int main(void){
    online=false;assert(check().state==STARTUP_NET_OFFLINE && !creates);online=true;
    startup_network_result_t r=check();assert(r.state==STARTUP_NET_READY && r.http_status==200 && closes==1 && cleanups==1);
    beta=true;r=check();assert(r.state==STARTUP_NET_READY);beta=false;
    status=503;r=check();assert(r.state==STARTUP_NET_FAILED && r.http_status==503);status=200;
    headers=-1;assert(check().state==STARTUP_NET_FAILED);headers=4100000;
    open_error=ESP_FAIL;transport=0x8001;socket_error=54;tls=0x3000;flags=4;
    r=check();assert(r.state==STARTUP_NET_FAILED && r.transport_error==0x8001 && r.socket_errno==54 && r.tls_code==0x3000 && r.tls_flags==4);open_error=ESP_OK;
    init_fail=true;r=check();assert(r.state==STARTUP_NET_FAILED && r.error==ESP_ERR_NO_MEM);init_fail=false;
    create_fail=true;r=check();assert(r.state==STARTUP_NET_FAILED && r.error==ESP_ERR_NO_MEM);create_fail=false;
    hold=true;int before=creates;r=check();assert(r.state==STARTUP_NET_TIMEOUT && now==8500000 && queued && creates==before+1);
    r=check();assert(r.state==STARTUP_NET_TIMEOUT && creates==before+1);
    before=cleanups;run();assert(cleanups==before);hold=false;
    r=check();assert(r.state==STARTUP_NET_READY && !handles && !queued && deletes>=8);
    puts("Boot networking: verified HTTPS HEAD/stable/beta, no body or flash, IP offline, HTTP/TLS/DNS/TCP failures, timeout/cancel/owner cleanup and no duplicate worker passed");
}
