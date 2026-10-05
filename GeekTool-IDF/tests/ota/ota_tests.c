#include "ota_update.h"
#include "sdk.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define SIZE 8192
enum scenario { NORMAL, DISCONNECT, NO_ETAG, FALLBACK, ALWAYS_DROP, CONNECT_ONCE,
    CERTIFICATE, NO_MEMORY, HTTP404, REPLACED, WRONG_RANGE, HEADER_FAIL,
    BAD_PROJECT, SAME_VERSION, TOO_BIG, TRUNCATED, STALL, VERIFY_FAIL };
static enum scenario scenario;
static int injected_tls=MBEDTLS_ERR_X509_CERT_VERIFY_FAILED, injected_flags=4;
struct mock_client { esp_http_client_config_t config; int status, tls, flags; char if_match[96]; };
static struct mock_client client;
static esp_partition_t partition;
static esp_app_desc_t current, remote;
static int begins, aborts, finishes, writes, progress, live;
static int requested[OTA_UPDATE_ATTEMPTS], resumed_pct;
static bool started;
static const char *current_version, *remote_version;
static int64_t clock_us;
static unsigned char flash[SIZE], expected[SIZE];

static void event(esp_http_client_event_id_t id, char *key, char *value) {
    esp_http_client_event_t e = { .event_id=id, .client=&client, .user_data=client.config.user_data,
                                .header_key=key, .header_value=value };
    assert(client.config.event_handler(&e)==ESP_OK);
}
esp_err_t esp_crt_bundle_attach(void *p) { (void)p; return ESP_OK; }
int esp_http_client_get_status_code(esp_http_client_handle_t c) { return c->status; }
esp_err_t esp_http_client_get_user_data(esp_http_client_handle_t c, void **data) { *data=c->config.user_data; return ESP_OK; }
esp_err_t esp_http_client_get_and_clear_last_tls_error(esp_http_client_handle_t c,int *code,int *flags) {
    *code=c->tls; *flags=c->flags; c->tls=c->flags=0; return ESP_OK;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c,const char *key,const char *value) {
    if (strcmp(key,"If-Match")==0) snprintf(c->if_match,sizeof c->if_match,"%s",value);
    return ESP_OK;
}
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *p) { (void)p; return &partition; }
const esp_app_desc_t *esp_app_get_description(void) { return &current; }
int64_t esp_timer_get_time(void) { return clock_us; }
void vTaskDelay(int ms) { clock_us+=(int64_t)ms*1000; }

esp_err_t esp_https_ota_begin(const esp_https_ota_config_t *cfg,esp_https_ota_handle_t *h) {
    assert(!live && begins<OTA_UPDATE_ATTEMPTS);
    assert(cfg->http_config->crt_bundle_attach && cfg->ota_resumption && cfg->partition.staging==&partition);
    assert(cfg->buffer_caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    requested[begins++]=(int)cfg->ota_image_bytes_written;
    client=(struct mock_client){ .config=*cfg->http_config, .status=206 };
    assert(cfg->http_client_init_cb(&client)==ESP_OK);
    if (cfg->ota_image_bytes_written) assert(strcmp(client.if_match,"\"image-1\"")==0);
    *h=NULL;
    if ((scenario==CONNECT_ONCE && begins==1) || scenario==CERTIFICATE || scenario==NO_MEMORY || scenario==HTTP404) {
        if (scenario==CERTIFICATE) { client.tls=injected_tls; client.flags=injected_flags; }
        if (scenario==HTTP404) client.status=404;
        event(HTTP_EVENT_ERROR,NULL,NULL); event(HTTP_EVENT_DISCONNECTED,NULL,NULL);
        return scenario==NO_MEMORY ? ESP_ERR_NO_MEM : ESP_ERR_HTTP_CONNECT;
    }
    if (scenario==REPLACED && begins==2) {
        client.status=412; event(HTTP_EVENT_DISCONNECTED,NULL,NULL); return ESP_FAIL;
    }
    // 按 SDK 契约:恢复请求被忽略时已回退到从零开始的 200 响应。
    if (scenario==FALLBACK && begins==2) client.status=200;
    progress=client.status==200 ? 0 : (int)cfg->ota_image_bytes_written;
    started=false; live=1; *h=&client;
    event(HTTP_EVENT_HEADERS_SENT,NULL,NULL);
    if (scenario!=NO_ETAG) event(HTTP_EVENT_ON_HEADER,"ETag","\"image-1\"");
    if (client.status==206) {
        char range[64]; snprintf(range,sizeof range,"bytes %d-%d/%d",scenario==WRONG_RANGE?1:progress,SIZE-1,SIZE);
        event(HTTP_EVENT_ON_HEADER,"Content-Range",range);
    }
    event(HTTP_EVENT_ON_HEADERS_COMPLETE,NULL,NULL);
    return ESP_OK;
}
esp_err_t esp_https_ota_get_img_desc(esp_https_ota_handle_t h,esp_app_desc_t *desc) {
    assert(h && live);
    if (scenario==HEADER_FAIL) return ESP_FAIL;
    *desc=remote; return ESP_OK;
}
int esp_https_ota_get_status_code(esp_https_ota_handle_t h) { assert(h && live); return client.status; }
int esp_https_ota_get_image_size(esp_https_ota_handle_t h) {
    assert(h && live); return SIZE-progress; // 非 partial 的 SDK 对恢复请求返回尾部 Content-Length
}
int esp_https_ota_get_image_len_read(esp_https_ota_handle_t h) { assert(h && live); return started?progress:-1; }
esp_err_t esp_https_ota_perform(esp_https_ota_handle_t h) {
    assert(h && live);
    if (scenario==STALL && progress>=1024) { clock_us+=15000000; return ESP_ERR_HTTPS_OTA_IN_PROGRESS; }
    if ((scenario==DISCONNECT || scenario==NO_ETAG || scenario==FALLBACK) && begins==1 && progress>=3072) return ESP_FAIL;
    if ((scenario==ALWAYS_DROP || scenario==REPLACED) && progress>=3072) return ESP_FAIL;
    if (scenario==TRUNCATED && progress>=SIZE-1024) return ESP_OK;
    if (progress==SIZE) return ESP_OK;
    memcpy(flash+progress,expected+progress,1024);
    progress+=1024; writes++; started=true;
    return ESP_ERR_HTTPS_OTA_IN_PROGRESS;
}
bool esp_https_ota_is_complete_data_received(esp_https_ota_handle_t h) {
    assert(h && live); return progress==SIZE;
}
esp_err_t esp_https_ota_finish(esp_https_ota_handle_t h) {
    assert(h && live && progress==SIZE);
    assert(memcmp(flash,expected,SIZE)==0);
    finishes++; live=0;
    return scenario==VERIFY_FAIL ? ESP_ERR_OTA_VALIDATE_FAILED : ESP_OK;
}
esp_err_t esp_https_ota_abort(esp_https_ota_handle_t h) { assert(h && live); aborts++; live=0; return ESP_OK; }

static void observe(const ota_status_t *s,void *user) {
    (void)user;
    assert(s->pct>=0 && s->pct<=100);
    if (s->state!=OTA_OK) assert(s->pct<100);
    if (s->state==OTA_RUNNING && s->attempt==2 && resumed_pct<0) resumed_pct=s->pct;
}
static ota_status_t run(enum scenario which) {
    scenario=which; begins=aborts=finishes=writes=progress=live=0; resumed_pct=-1; clock_us=0;
    memset(requested,0,sizeof requested); memset(flash,0xdd,sizeof flash);
    partition=(esp_partition_t){ .size=which==TOO_BIG ? SIZE/2 : SIZE };
    current=(esp_app_desc_t){ .magic_word=ESP_APP_DESC_MAGIC_WORD, .version="v1.7-beta.7", .project_name="GeekTool" };
    remote=(esp_app_desc_t){ .magic_word=ESP_APP_DESC_MAGIC_WORD, .version="v1.7-beta.8", .project_name="GeekTool", .app_elf_sha256={1} };
    if (current_version) snprintf(current.version,sizeof current.version,"%s",current_version);
    if (remote_version) snprintf(remote.version,sizeof remote.version,"%s",remote_version);
    if (which==BAD_PROJECT) strcpy(remote.project_name,"other device");
    if (which==SAME_VERSION) strcpy(remote.version,current.version);
    ota_status_t s=ota_update_run("https://test.invalid/firmware.bin",observe,NULL);
    assert(!live); return s;
}
static void version_case(const char *from,const char *to,bool same) {
    current_version=from;remote_version=to;
    ota_status_t s=run(NORMAL);
    current_version=remote_version=NULL;
    assert(begins==1);
    if (same) assert(s.state==OTA_UPTODATE && writes==0 && finishes==0 && aborts==1);
    else assert(s.state==OTA_OK && writes==8 && finishes==1 && aborts==0);
}
int main(void) {
    for (int i=0;i<SIZE;i++) expected[i]=(unsigned char)(i*37+17);
    ota_status_t s=run(NORMAL); assert(s.state==OTA_OK && s.pct==100 && begins==1 && finishes==1 && aborts==0);
    s=run(DISCONNECT); assert(s.state==OTA_OK && begins==2 && requested[1]==3072 && resumed_pct==37 && finishes==1 && aborts==1);
    s=run(NO_ETAG); assert(s.state==OTA_OK && requested[1]==0 && resumed_pct==0);
    s=run(FALLBACK); assert(s.state==OTA_OK && requested[1]==3072 && resumed_pct==0);
    s=run(ALWAYS_DROP); assert(s.state==OTA_FAIL && begins==3 && aborts==3 && finishes==0);
    s=run(CONNECT_ONCE); assert(s.state==OTA_OK && begins==2);
    s=run(CERTIFICATE); assert(s.state==OTA_FAIL && begins==1 && s.tls_flags==4 && finishes==0);
    // The real ESP-TLS backend captures -ret, i.e. positive TLS magnitudes.
    // Test both signs without flags so certificate/allocation errors never retry.
    const int fatal_tls[]={0x2700,-0x2700,0x3000,-0x3000,141,-141};
    injected_flags=0;
    for (unsigned i=0;i<sizeof fatal_tls/sizeof fatal_tls[0];i++) {
        injected_tls=fatal_tls[i]; s=run(CERTIFICATE);
        assert(s.state==OTA_FAIL && begins==1 && s.tls_code==injected_tls && finishes==0 && writes==0);
    }
    s=run(NO_MEMORY); assert(s.state==OTA_FAIL && begins==1 && s.error==ESP_ERR_NO_MEM);
    s=run(HTTP404); assert(s.state==OTA_FAIL && begins==1 && s.http_status==404);
    s=run(REPLACED); assert(s.state==OTA_FAIL && begins==2 && s.http_status==412 && finishes==0 && writes==3);
    s=run(WRONG_RANGE); assert(s.state==OTA_FAIL && begins==1 && writes==0 && finishes==0);
    s=run(HEADER_FAIL); assert(s.state==OTA_FAIL && begins==3 && writes==0 && finishes==0);
    s=run(BAD_PROJECT); assert(s.state==OTA_FAIL && begins==1 && writes==0 && s.error==ESP_ERR_INVALID_VERSION);
    s=run(SAME_VERSION); assert(s.state==OTA_UPTODATE && begins==1 && writes==0 && finishes==0 && aborts==1);
    s=run(TOO_BIG); assert(s.state==OTA_FAIL && begins==1 && writes==0 && s.error==ESP_ERR_INVALID_SIZE);
    s=run(TRUNCATED); assert(s.state==OTA_FAIL && begins==3 && finishes==0);
    s=run(STALL); assert(s.state==OTA_FAIL && begins==3 && s.error==ESP_ERR_TIMEOUT && finishes==0);
    s=run(VERIFY_FAIL); assert(s.state==OTA_FAIL && begins==1 && finishes==1 && aborts==0 && s.failed_at==OTA_VERIFYING);
    // Local git-describe suffixes must not replace unpublished fixes with the tag's old image.
    version_case("v1.7-beta.23-1-gfb70add-dirty","v1.7-beta.23",true);
    version_case("v1.7-beta.23-dirty","v1.7-beta.23",true);
    version_case("v1.7-beta.23-2-g0123abc","v1.7-beta.23",true);
    version_case("v1.7-beta.23","v1.7-beta.23-2-g0123abc",true);
    version_case("v1.7-15-gabcdef0-dirty","v1.7",true);
    version_case("v1.7-beta.23-1-gfb70add-dirty","v1.7-beta.24",false);
    version_case("v1.7-beta.23-1-gxyz","v1.7-beta.23",false);
    version_case("v1.7-beta.23-x-g0123abc","v1.7-beta.23",false);
    version_case("v1.7-beta.23-dirty-extra","v1.7-beta.23",false);
    // Selecting a different release channel retains the existing update behavior.
    version_case("v1.7-beta.23","v1.6.1",false);
    puts("18 OTA recovery groups, six signed TLS errors and ten local-build version cases passed");
    return 0;
}
