// 下载恢复仅存本轮任务的 RAM,不增加 NVS 格式。仅校验完成后切换 boot 分区。
#include "ota_update.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#if !defined(CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC)
#error "OTA requires TLS PSRAM allocation; preserve the approved OTA memory policy"
#endif
#endif

static const char *TAG = "ota";
#define OTA_RESUME_MIN 1024       // ESP-IDF DEFAULT_OTA_BUF_SIZE;头读入 RAM 不代表已经写入 Flash
#define OTA_STALL_US (45LL * 1000000)

typedef struct {
    char etag[96], expected_etag[96];
    int range_start, range_total, http_status, tls_code, tls_flags;
} http_status_t;

static esp_err_t http_event(esp_http_client_event_t *e) {
    http_status_t *s = e->user_data;
    if (e->event_id == HTTP_EVENT_HEADERS_SENT) {
        s->etag[0] = 0; s->range_start = s->range_total = -1;
    } else if (e->event_id == HTTP_EVENT_ON_HEADER) {
        if (strcasecmp(e->header_key, "ETag") == 0) {
            size_t n = strlen(e->header_value);
            if (n < sizeof s->etag) memcpy(s->etag, e->header_value, n + 1);
        } else if (strcasecmp(e->header_key, "Content-Range") == 0) {
            long long start, end, total; int consumed = 0;
            if (sscanf(e->header_value, "bytes %lld-%lld/%lld%n", &start, &end, &total, &consumed) == 3 &&
                e->header_value[consumed] == 0 && start >= 0 && end >= start && total > end && total <= INT_MAX) {
                s->range_start = (int)start; s->range_total = (int)total;
            }
        }
    } else if (e->event_id == HTTP_EVENT_ON_HEADERS_COMPLETE) {
        s->http_status = esp_http_client_get_status_code(e->client);
    } else if (e->event_id == HTTP_EVENT_ERROR || e->event_id == HTTP_EVENT_DISCONNECTED) {
        int code = 0, flags = 0;
        esp_http_client_get_and_clear_last_tls_error(e->client, &code, &flags);
        if (code) s->tls_code = code;
        if (flags) s->tls_flags = flags;
        int status = esp_http_client_get_status_code(e->client);
        if (status > 0) s->http_status = status;
    }
    return ESP_OK;
}

// init_cb 没有 user 参数;同一任务内 HTTP_EVENT_ON_CONNECTED 前需要设置请求头。
// 使用 http_config.user_data 经客户端公开 API 取回,不依赖全局可变请求状态。
static esp_err_t http_init(esp_http_client_handle_t client) {
    http_status_t *s = NULL;
    esp_err_t data_err = esp_http_client_get_user_data(client, (void **)&s);
    if (data_err != ESP_OK || !s) return ESP_ERR_INVALID_ARG;
    esp_err_t err = esp_http_client_set_header(client, "Accept-Encoding", "identity");
    if (err == ESP_OK) err = esp_http_client_set_header(client, "Cache-Control", "no-cache");
    if (err == ESP_OK && s->expected_etag[0]) err = esp_http_client_set_header(client, "If-Match", s->expected_etag);
    return err;
}

static void publish(ota_status_t *s, ota_state_t state, ota_status_cb cb, void *user) {
    s->state = state;
    if (cb) cb(s, user);
}

// ESP-IDF's git describe adds -<commits>-g<hash> and -dirty to a local tag.
// Compare that tag so a local fix cannot be replaced by its already-published image.
// Other version differences still follow the selected release channel's behavior.
static size_t version_tag_length(const char *version) {
    size_t n = strnlen(version, sizeof(((esp_app_desc_t *)0)->version));
    if (n > 6 && memcmp(version + n - 6, "-dirty", 6) == 0) n -= 6;
    size_t hash = n;
    while (hash && ((version[hash-1] >= '0' && version[hash-1] <= '9') ||
                   (version[hash-1] >= 'a' && version[hash-1] <= 'f'))) hash--;
    if (hash >= 2 && hash < n && version[hash-2] == '-' && version[hash-1] == 'g') {
        size_t count_end = hash - 2, count = count_end;
        while (count && version[count-1] >= '0' && version[count-1] <= '9') count--;
        if (count > 1 && count < count_end && version[count-1] == '-') n = count - 1;
    }
    return n;
}

static bool same_version_tag(const char *left, const char *right) {
    size_t n = version_tag_length(left);
    return n && n == version_tag_length(right) && memcmp(left, right, n) == 0;
}

static bool can_retry(const ota_status_t *s) {
    if (s->attempt >= OTA_UPDATE_ATTEMPTS || s->failed_at == OTA_VERIFYING || s->tls_flags ||
        ota_tls_certificate_error(s->tls_code) ||
        ota_tls_error_magnitude(s->tls_code) == ota_tls_error_magnitude(MBEDTLS_ERR_SSL_ALLOC_FAILED) ||
        ota_tls_error_magnitude(s->tls_code) == ota_tls_error_magnitude(MBEDTLS_ERR_X509_ALLOC_FAILED) ||
        s->error == ESP_ERR_NO_MEM) return false;
    if (s->http_status >= 400 && s->http_status < 500 && s->http_status != 408 && s->http_status != 429) return false;
    // esp_https_ota 的断流/不完整数据均返回 ESP_FAIL;Flash/镜像/参数错误有各自错误码。
    return s->error == ESP_FAIL || s->error == ESP_ERR_TIMEOUT || s->error == ESP_ERR_HTTP_CONNECT ||
           s->error == ESP_ERR_HTTP_FETCH_HEADER || s->error == ESP_ERR_HTTP_CONNECTION_CLOSED ||
           s->error == ESP_ERR_HTTP_EAGAIN;
}

ota_status_t ota_update_run(const char *url, ota_status_cb cb, void *user) {
    ota_status_t s = { .state = OTA_CHECKING };
    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    const esp_app_desc_t *current = esp_app_get_description();
    esp_app_desc_t identity = {0};
    char etag[96] = {0};
    int written = 0, image_size = 0;
    if (!partition) {
        s.failed_at = OTA_VERIFYING; s.error = ESP_ERR_NOT_FOUND;
        publish(&s, OTA_FAIL, cb, user); return s;
    }

    for (s.attempt = 1; s.attempt <= OTA_UPDATE_ATTEMPTS; s.attempt++) {
        http_status_t response = { .range_start = -1, .range_total = -1 };
        if (written) memcpy(response.expected_etag, etag, sizeof etag);
        esp_http_client_config_t http = {
            .url = url, .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = 15000, .keep_alive_enable = true,
            .buffer_size = 4096, .buffer_size_tx = 1024,
            .event_handler = http_event, .user_data = &response,
        };
        esp_https_ota_config_t cfg = {
            .http_config = &http, .http_client_init_cb = http_init,
            .buffer_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
            .ota_resumption = true, .ota_image_bytes_written = written,
            .partition.staging = partition,
        };
        esp_https_ota_handle_t h = NULL;
        s.http_status = s.tls_code = s.tls_flags = 0;
        s.failed_at = OTA_CHECKING;
        publish(&s, OTA_CHECKING, cb, user);
        ESP_LOGI(TAG, "attempt %d/%d offset=%d internal free=%u largest=%u psram free=%u largest=%u", s.attempt,
                 OTA_UPDATE_ATTEMPTS, written, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
        s.error = esp_https_ota_begin(&cfg, &h);
        if (s.error != ESP_OK) goto failed;
        response.http_status = esp_https_ota_get_status_code(h);

        // SDK 遇到 200/416 会自动从零重下;不能把旧断点加到新进度上。
        if (response.http_status == 200) written = 0;
        if (response.http_status != 200 && (response.http_status != 206 || response.range_start != written)) {
            s.error = ESP_ERR_INVALID_RESPONSE; goto failed;
        }
        int total = response.http_status == 206 ? response.range_total : esp_https_ota_get_image_size(h);
        if (total <= 0 || (size_t)total > partition->size) {
            s.failed_at = OTA_HEADER; s.error = ESP_ERR_INVALID_SIZE; goto failed;
        }
        if (image_size && total != image_size) { s.error = ESP_ERR_INVALID_VERSION; goto failed; }
        // 相同 URL 可被发布覆盖;断点必须属于相同对象,否则绝不能拼接新旧固件。
        if (written && strcmp(etag, response.etag) != 0) { s.error = ESP_ERR_INVALID_VERSION; goto failed; }
        image_size = total;

        s.failed_at = OTA_HEADER;
        publish(&s, OTA_HEADER, cb, user);
        esp_app_desc_t remote;
        s.error = esp_https_ota_get_img_desc(h, &remote);
        if (s.error != ESP_OK) goto failed;
        if (remote.magic_word != ESP_APP_DESC_MAGIC_WORD || !memchr(remote.version, 0, sizeof remote.version) ||
            !remote.version[0] || strncmp(remote.project_name, current->project_name, sizeof remote.project_name) != 0) {
            s.error = ESP_ERR_INVALID_VERSION; goto failed;
        }
        if (written && memcmp(identity.app_elf_sha256, remote.app_elf_sha256, sizeof remote.app_elf_sha256) != 0) {
            s.error = ESP_ERR_INVALID_VERSION; goto failed;
        }
        identity = remote;
        memcpy(etag, response.etag, sizeof etag);
        memcpy(s.version, remote.version, sizeof s.version);
        if (same_version_tag(remote.version, current->version)) {
            esp_https_ota_abort(h);
            publish(&s, OTA_UPTODATE, cb, user); return s;
        }

        s.failed_at = OTA_RUNNING;
        s.pct = written * 100LL / image_size;
        publish(&s, OTA_RUNNING, cb, user);
        int64_t last_progress = esp_timer_get_time();
        while (1) {
            s.error = esp_https_ota_perform(h);
            int n = esp_https_ota_get_image_len_read(h); // 仅成功写入后推进,不可用头部预读量续传
            if (n > written) { written = n; last_progress = esp_timer_get_time(); }
            s.pct = (int)(written * 100LL / image_size);
            if (s.pct > 99) s.pct = 99;                // 校验和切启动分区前不宣称完成
            if (s.error != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;
            if (esp_timer_get_time() - last_progress >= OTA_STALL_US) { s.error = ESP_ERR_TIMEOUT; break; }
            publish(&s, OTA_RUNNING, cb, user);
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        if (s.error != ESP_OK) goto failed;
        if (!esp_https_ota_is_complete_data_received(h) || written != image_size) {
            s.error = ESP_FAIL; goto failed;
        }
        s.failed_at = OTA_VERIFYING;
        publish(&s, OTA_VERIFYING, cb, user);
        s.error = esp_https_ota_finish(h); h = NULL;    // finish 包含校验、切分区和句柄释放
        if (s.error != ESP_OK) goto failed;
        s.pct = 100;
        publish(&s, OTA_OK, cb, user); return s;

failed:
        if (h) {
            esp_err_t cleanup = esp_https_ota_abort(h);
            if (cleanup != ESP_OK) ESP_LOGW(TAG, "abort: %s", esp_err_to_name(cleanup));
        }
        s.http_status = response.http_status; s.tls_code = response.tls_code; s.tls_flags = response.tls_flags;
        ESP_LOGE(TAG, "stage=%d attempt=%d bytes=%d/%d err=%s HTTP=%d TLS=%d flags=0x%x", s.failed_at,
                 s.attempt, written, image_size, esp_err_to_name(s.error), s.http_status, s.tls_code, s.tls_flags);
        if (!can_retry(&s)) break;
        // 弱 ETag/缺少身份或还没写够头部时从零重下;不猜断点,不保存跨重启状态。
        if (written < OTA_RESUME_MIN || written >= image_size || etag[0] != '"') written = 0;
        publish(&s, OTA_RETRYING, cb, user);
        vTaskDelay(pdMS_TO_TICKS(s.attempt * 1000));
    }
    publish(&s, OTA_FAIL, cb, user);
    return s;
}
