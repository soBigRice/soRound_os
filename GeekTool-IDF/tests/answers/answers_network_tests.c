#include "answers_data.h"
#include "network_http.h"
#include "answers_test_sdk.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *payload="{\"code\":200,\"data\":{\"title_en\":\"Stay resilient.\",\"title_zh\":\"保持弹性\"}}";
static struct mock_client { size_t at; } client;
static bool ip_ready=true,first_attempt=true;
static int open_fail_count,status_fail_count,init_calls,tls_flags,tls_code,transport_error,socket_error,timeout_ms;
static int64_t open_delay;
static bool online=true,create_fail,init_fail,open_fail,header_fail,read_fail,complete=true,cancel_on_read;
static int status=200,handles,opens,closes,cleanups,deletes;
static int64_t declared=-2,clock_us,read_delay;
static void (*queued)(void *);
static char previous_url[128];
static uint32_t random_value=0x17483920;
uint32_t esp_random(void){return ++random_value;}
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap){(void)ap;return online?ESP_OK:ESP_FAIL;}
bool wifi_service_ready(void){return online && ip_ready;}
void vTaskDelay(int ms){clock_us+=(int64_t)ms*1000;}
int esp_http_client_get_errno(esp_http_client_handle_t h){assert(h==&client);return socket_error;}
esp_err_t esp_http_client_get_and_clear_last_tls_error(esp_http_client_handle_t h,int *code,int *flags){assert(h==&client);*code=tls_code;*flags=tls_flags;tls_code=tls_flags=0;return transport_error;}
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t h,int ms){assert(h==&client && ms>0 && ms<=6000);timeout_ms=ms;return ESP_OK;}
esp_err_t esp_crt_bundle_attach(void *p){(void)p;return ESP_OK;}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg){
    size_t prefix=strlen(ANSWER_API_URL);
    assert(!strncmp(cfg->url,ANSWER_API_URL,prefix)&&cfg->crt_bundle_attach==esp_crt_bundle_attach);
    assert(strlen(cfg->url)==prefix+19&&!strncmp(cfg->url+prefix,"%20",3));
    for(size_t i=prefix+3;i<prefix+19;++i)
        assert((cfg->url[i]>='0'&&cfg->url[i]<='9')||(cfg->url[i]>='a'&&cfg->url[i]<='f'));
    if(first_attempt){assert(strcmp(cfg->url,previous_url));snprintf(previous_url,sizeof previous_url,"%s",cfg->url);first_attempt=false;}
    else assert(!strcmp(cfg->url,previous_url));
    assert(cfg->timeout_ms==6000&&cfg->disable_auto_redirect);
    ++init_calls;
    if(init_fail)return NULL;assert(!handles);++handles;client.at=0;return &client;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t h,const char *key,const char *value){
    assert(h==&client&&key&&value);return header_fail?ESP_FAIL:ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t h,int length){
    assert(h==&client&&length==0);clock_us+=open_delay;
    if(open_fail)return ESP_FAIL;if(open_fail_count>0){--open_fail_count;return ESP_FAIL;}++opens;return ESP_OK;
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t h){assert(h==&client);return declared==-2?(int64_t)strlen(payload):declared;}
int esp_http_client_get_status_code(esp_http_client_handle_t h){assert(h==&client);if(status_fail_count>0){--status_fail_count;return 503;}return status;}
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
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t h){assert(h==&client&&handles==1);--handles;++cleanups;tls_code=tls_flags=transport_error=socket_error=0;return ESP_OK;}
int64_t esp_timer_get_time(void){return clock_us;}
int xTaskCreate(void (*task)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle){
    (void)name;(void)priority;(void)handle;assert(stack==8192&&!arg&&!queued);
    if(create_fail)return 0;first_attempt=true;queued=task;return pdPASS;
}
void vTaskDelete(void *p){assert(!p);++deletes;}
static void run(void){assert(queued);void (*task)(void *)=queued;queued=NULL;task(NULL);assert(!handles);}
static uint32_t request(void){uint32_t token;assert(answers_fetch_begin(&token)==ANSWER_FETCH_LOADING);return token;}
static void failure(void){uint32_t token=request();run();assert(answers_fetch_poll(token,NULL)==ANSWER_FETCH_FAILED);}
static network_http_result_t diagnostic(void) {
    char url[128],body[ANSWER_HTTP_BYTES];first_attempt=true;
    snprintf(url,sizeof url,"%s%%20%08lx%08lx",ANSWER_API_URL,(unsigned long)esp_random(),(unsigned long)esp_random());
    esp_http_client_config_t cfg={.url=url,.crt_bundle_attach=esp_crt_bundle_attach,.timeout_ms=6000,.disable_auto_redirect=true};
    return network_http_get("test",&cfg,body,sizeof body,8000,NULL,NULL);
}
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
    ip_ready=false;assert(answers_fetch_begin(&token)==ANSWER_FETCH_OFFLINE&&!queued);ip_ready=true;
    int count=init_calls;open_fail_count=1;token=request();run();
    assert(answers_fetch_poll(token,&out)==ANSWER_FETCH_READY && init_calls==count+2);
    count=init_calls;status_fail_count=1;token=request();run();
    assert(answers_fetch_poll(token,&out)==ANSWER_FETCH_READY && init_calls==count+2);
    count=init_calls;open_fail=true;tls_flags=4;failure();assert(init_calls==count+1);tls_flags=0;open_fail=false;
    count=init_calls;status=404;failure();assert(init_calls==count+1);status=200;
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
    const int fatal_tls[]={-0x2700,0x2700,-0x3000,0x3000,-141,141};
    for(unsigned i=0;i<sizeof fatal_tls/sizeof fatal_tls[0];++i) {
        int before=init_calls;open_fail=true;tls_code=fatal_tls[i];
        network_http_result_t d=diagnostic();assert(d.stage==NET_HTTP_CONNECT && d.tls_code==fatal_tls[i] && init_calls==before+1);
        open_fail=false;
    }
    open_fail=true;tls_flags=4;transport_error=0x8001;socket_error=54;
    network_http_result_t d=diagnostic();assert(d.tls_flags==4 && d.transport_error==0x8001 && d.socket_errno==54);
    open_fail=false;
    open_fail_count=1;open_delay=6000000;int before=init_calls;
    d=diagnostic();assert(d.stage==NET_HTTP_TIMEOUT && init_calls==before+2 && timeout_ms<=1800);open_delay=0;
    assert(!handles&&!queued&&opens==closes&&cleanups>opens&&deletes>=16);
    puts("answers network: unique per-request URL, TLS config, bounded titles/UTF8/JSON, live task branch, offline/busy/create/init/header/open/HTTP/read/truncation/size/deadline failures, chunked completion, cancel/stale results and cleanup passed");
}
