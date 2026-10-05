#include "zodiac_data.h"
#include "answers_test_sdk.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static char fixture[8192];
static const char *payload=fixture;
time_t zodiac_test_time(time_t *out){struct tm t={.tm_year=126,.tm_mon=9,.tm_mday=5,.tm_hour=12};time_t n=mktime(&t);if(out)*out=n;return n;}

static struct mock_client { size_t at; } client;
static bool online=true,create_fail,init_fail,open_fail,header_fail,read_fail,complete=true,cancel_on_read;
static int status=200,handles,opens,closes,cleanups,deletes;
static int64_t declared=-2,clock_us,read_delay;
static void (*queued)(void *);
static uint32_t random_value=0x17483920;
uint32_t esp_random(void){return ++random_value;}
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap){(void)ap;return online?ESP_OK:ESP_FAIL;}
esp_err_t esp_crt_bundle_attach(void *p){(void)p;return ESP_OK;}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg){
    assert(!strcmp(cfg->url,"https://v2.xxapi.cn/api/horoscope?type=aries&time=today&date=20261005"));
    assert(cfg->crt_bundle_attach==esp_crt_bundle_attach);
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
    if(cancel_on_read){cancel_on_read=false;zodiac_fetch_cancel();}
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
static uint32_t request(void){uint32_t token;assert(zodiac_fetch_begin(0,&token)==ZODIAC_LOADING);return token;}
static void failure(void){uint32_t token=request();run();assert(zodiac_fetch_poll(token,NULL)==ZODIAC_FAILED);}
int main(void){
    FILE *f=fopen(ZODIAC_FIXTURE_PATH,"rb");assert(f);size_t n=fread(fixture,1,sizeof fixture-1,f);fclose(f);fixture[n]=0;
    zodiac_data_t out={0};uint32_t token;
    assert(zodiac_data_parse(payload,strlen(payload),0,&out));assert(!strcmp(out.date,"10月5日")&&out.score[0]==4);
    assert(!strcmp(out.color,"草绿")&&!strcmp(out.number,"17"));
    assert(!zodiac_data_parse(payload,strlen(payload),1,&out));
    assert(!zodiac_data_parse(payload,ZODIAC_HTTP_BYTES,0,&out));
    const char *bad[]={"{}","not JSON","{\"code\":500,\"data\":{}}",
      "{\"code\":200,\"data\":{\"name\":\"aries\",\"time\":\"10月5日\",\"shortcomment\":\"\xc0\xaf\"}}"};
    for(unsigned i=0;i<sizeof bad/sizeof bad[0];++i)assert(!zodiac_data_parse(bad[i],strlen(bad[i]),0,&out));
    online=false;assert(zodiac_fetch_begin(0,&token)==ZODIAC_OFFLINE&&!queued);online=true;
    assert(zodiac_fetch_begin(12,&token)==ZODIAC_FAILED&&!queued);
    create_fail=true;assert(zodiac_fetch_begin(0,&token)==ZODIAC_FAILED&&!queued);create_fail=false;
    token=request();assert(zodiac_fetch_begin(0,&token)==ZODIAC_BUSY);run();
    assert(zodiac_fetch_poll(token,&out)==ZODIAC_READY&&!strcmp(out.date,"10月5日"));
    init_fail=true;failure();init_fail=false;
    header_fail=true;failure();header_fail=false;
    open_fail=true;failure();open_fail=false;
    status=500;failure();status=200;
    declared=ZODIAC_HTTP_BYTES;failure();declared=-1;failure();declared=-2;
    read_fail=true;failure();read_fail=false;
    complete=false;failure();complete=true;
    read_delay=9000000;failure();read_delay=0;
    const char *valid=payload;payload="{\"code\":429,\"data\":{}}";failure();payload=valid;
    char *date=strstr(fixture,"10月5日");assert(date);date[5]='4';failure();date[5]='5';
    declared=0;token=request();run();assert(zodiac_fetch_poll(token,&out)==ZODIAC_READY);declared=-2;
    token=request();zodiac_fetch_cancel();run();assert(zodiac_fetch_poll(token,NULL)==ZODIAC_IDLE);
    token=request();cancel_on_read=true;run();assert(zodiac_fetch_poll(token,NULL)==ZODIAC_IDLE);
    char oversized[ZODIAC_HTTP_BYTES+20];memset(oversized,'x',sizeof oversized-1);oversized[sizeof oversized-1]=0;
    payload=oversized;declared=0;failure();payload=valid;declared=-2;
    token=request();run();assert(zodiac_fetch_poll(token,&out)==ZODIAC_READY);
    assert(!handles&&!queued&&opens==closes&&cleanups>opens&&deletes>=16);
    puts("zodiac network: actual API fixture, requested sign/day checks, TLS, bounded JSON/UTF8, HTTP/connection/deadline/offline/busy failures, cancellation, stale results and cleanup passed");
}
