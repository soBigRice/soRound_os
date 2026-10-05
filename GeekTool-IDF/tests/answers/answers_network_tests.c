#include "answers_data.h"
#include "answers_test_sdk.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *payload="{\"code\":200,\"data\":{\"title_en\":\"Stay resilient.\",\"title_zh\":\"保持弹性\"}}";
static struct mock_client { size_t at; } client;
static bool online=true,create_fail,init_fail,open_fail,header_fail,read_fail,complete=true,cancel_on_read;
static int status=200,handles,opens,closes,cleanups,deletes;
static int64_t declared=-2,clock_us,read_delay;
static void (*queued)(void *);
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap){(void)ap;return online?ESP_OK:ESP_FAIL;}
esp_err_t esp_crt_bundle_attach(void *p){(void)p;return ESP_OK;}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg){
    assert(!strcmp(cfg->url,ANSWER_API_URL)&&cfg->crt_bundle_attach==esp_crt_bundle_attach);
    assert(cfg->timeout_ms==6000&&cfg->disable_auto_redirect);
    if(init_fail)return NULL;assert(!handles);++handles;client.at=0;return &client;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t h,const char *key,const char *value){
    assert(h==&client&&key&&value);return header_fail?ESP_FAIL:ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t h,int length){
    assert(h==&client&&length==0);if(open_fail)return ESP_FAIL;++opens;return ESP_OK;
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t h){assert(h==&client);return declared==-2?(int64_t)strlen(payload):declared;}
int esp_http_client_get_status_code(esp_http_client_handle_t h){assert(h==&client);return status;}
int esp_http_client_read(esp_http_client_handle_t h,char *out,int capacity){
    assert(h==&client&&capacity>0);clock_us+=read_delay;
    if(cancel_on_read){cancel_on_read=false;answers_fetch_cancel();}
    if(read_fail)return -1;
    size_t left=strlen(payload)-client.at,n=left>7?7:left;
    if(n>(size_t)capacity)n=(size_t)capacity;
    memcpy(out,payload+client.at,n);client.at+=n;return (int)n;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t h){assert(h==&client);return complete;}
esp_err_t esp_http_client_close(esp_http_client_handle_t h){assert(h==&client);++closes;return ESP_OK;}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t h){assert(h==&client&&handles==1);--handles;++cleanups;return ESP_OK;}
int64_t esp_timer_get_time(void){return clock_us;}
int xTaskCreate(void (*task)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle){
    (void)name;(void)priority;(void)handle;assert(stack==8192&&!arg&&!queued);
    if(create_fail)return 0;queued=task;return pdPASS;
}
void vTaskDelete(void *p){assert(!p);++deletes;}
static void run(void){assert(queued);void (*task)(void *)=queued;queued=NULL;task(NULL);assert(!handles);}
static uint32_t request(void){uint32_t token;assert(answers_fetch_begin(&token)==ANSWER_FETCH_LOADING);return token;}
static void failure(void){uint32_t token=request();run();assert(answers_fetch_poll(token,NULL)==ANSWER_FETCH_FAILED);}
int main(void){
    answer_response_t out={0};uint32_t token;
    assert(answers_data_parse(payload,strlen(payload),&out));assert(!strcmp(out.zh,"保持弹性"));
    const char *bad[]={"{}","not JSON","{\"code\":500,\"data\":{}}",
        "{\"code\":200,\"data\":{\"title_en\":1,\"title_zh\":\"好\"}}",
        "{\"code\":200,\"data\":{\"title_en\":\" \" ,\"title_zh\":\"好\"}}",
        "{\"code\":200,\"data\":{\"title_en\":\"line\\nline\",\"title_zh\":\"好\"}}",
        "{\"code\":200,\"data\":{\"title_en\":\"\xc0\xaf\",\"title_zh\":\"好\"}}"};
    for(unsigned i=0;i<sizeof bad/sizeof bad[0];++i)assert(!answers_data_parse(bad[i],strlen(bad[i]),&out));
    assert(!answers_data_parse(payload,ANSWER_HTTP_BYTES,&out));
    online=false;assert(answers_fetch_begin(&token)==ANSWER_FETCH_OFFLINE&&!queued);online=true;
    create_fail=true;assert(answers_fetch_begin(&token)==ANSWER_FETCH_FAILED&&!queued);create_fail=false;
    token=request();assert(answers_fetch_begin(&token)==ANSWER_FETCH_BUSY);run();
    assert(answers_fetch_poll(token,&out)==ANSWER_FETCH_READY&&!strcmp(out.en,"Stay resilient."));
    init_fail=true;failure();init_fail=false;
    header_fail=true;failure();header_fail=false;
    open_fail=true;failure();open_fail=false;
    status=500;failure();status=200;
    declared=ANSWER_HTTP_BYTES;failure();declared=-1;failure();declared=-2;
    read_fail=true;failure();read_fail=false;
    complete=false;failure();complete=true;
    read_delay=9000000;failure();read_delay=0;
    const char *valid=payload;payload="{\"code\":429,\"data\":{}}";failure();payload=valid;
    declared=0;token=request();run();assert(answers_fetch_poll(token,&out)==ANSWER_FETCH_READY);declared=-2;
    token=request();answers_fetch_cancel();run();assert(answers_fetch_poll(token,NULL)==ANSWER_FETCH_IDLE);
    token=request();cancel_on_read=true;run();assert(answers_fetch_poll(token,NULL)==ANSWER_FETCH_IDLE);
    char oversized[ANSWER_HTTP_BYTES+20];memset(oversized,'x',sizeof oversized-1);oversized[sizeof oversized-1]=0;
    payload=oversized;declared=0;failure();payload=valid;declared=-2;
    token=request();run();assert(answers_fetch_poll(token,&out)==ANSWER_FETCH_READY);
    assert(!handles&&!queued&&opens==closes&&cleanups>opens&&deletes>=16);
    puts("answers network: TLS config, bounded titles/UTF8/JSON, live task branch, offline/busy/create/init/header/open/HTTP/read/truncation/size/deadline failures, chunked completion, cancel/stale results and cleanup passed");
}
