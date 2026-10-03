// 天气 app —— Open-Meteo 上海实时天气,含昼夜/当日低高温/湿度。
// HTTP 独立任务只写数据;weather_tick 更新 weather_ui 的局部绘制控件。
#include "app.h"
#include "weather_ui.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

static const char *TAG = "weather";

#define CITY    "Shanghai"
#define WX_LAT  "31.2304"
#define WX_LON  "121.4737"
// Open-Meteo:无 key;数值字段;天气是 WMO code(timezone=auto 让当日低/高按本地日界)
// 用 HTTP 不用 HTTPS:天气是公开数据,免去 TLS 那 ~32KB 内存(S3 内部 RAM 被显存+WiFi 占满会 ssl_setup 失败)
#define WX_URL  "http://api.open-meteo.com/v1/forecast?latitude=" WX_LAT "&longitude=" WX_LON \
                "&current=temperature_2m,relative_humidity_2m,weather_code,is_day" \
                "&daily=temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=1"
#define WX_BUF  8192                                   // open-meteo 响应 ~1-2KB

typedef enum { WX_IDLE, WX_LOADING, WX_OK, WX_FAIL } wx_state_t;
static volatile wx_state_t s_state = WX_IDLE;
static volatile bool       s_task_alive = false;   // 拉取任务在跑?防反复进 app 起多个 8KB 栈任务堆积爆内存
static wx_state_t          s_shown = (wx_state_t)-1;
static volatile uint32_t   s_revision;
static uint32_t            s_shown_revision;
static int  s_temp_i = 0, s_lo = 0, s_hi = 0, s_hum_i = 0, s_code = -1;
static bool s_is_day = true;
static weather_ui_t s_ui;

/* ===== HTTP 拉取(独立任务)+ 极简 JSON 取值(免 cJSON 依赖) ===== */
// 取 "key":<number> 的数值(open-meteo 无引号数字;数组 [n,...] 跳过 '[' 取首个)。
// 调用方先把 body 指到 "current"/"daily" 段之后,避开前面的 *_units 段(同名键值是字符串)。
static float json_num(const char *body, const char *key, float fb) {
    if (!body) return fb;
    char pat[48]; snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(body, pat);
    if (!p) return fb;
    p += strlen(pat);
    while (*p == ' ' || *p == '[') p++;
    return (float)atof(p);
}
static void wx_task(void *arg) {
    (void)arg;
    esp_http_client_config_t cfg = {
        .url = WX_URL, .crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = 12000,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    char *body = heap_caps_malloc(WX_BUF, MALLOC_CAP_SPIRAM);   // 大缓冲放 PSRAM
    if (!body) body = malloc(WX_BUF);
    int total = 0;
    bool opened = false;
    if (!cli) {
        ESP_LOGE(TAG, "HTTP client init failed");
    } else if (!body) {
        ESP_LOGE(TAG, "weather buffer allocation failed");
    } else if (esp_http_client_open(cli, 0) == ESP_OK) {
        opened = true;
        esp_http_client_fetch_headers(cli);
        int nb;
        while ((nb = esp_http_client_read(cli, body + total, WX_BUF - 1 - total)) > 0) {
            total += nb;
            if (total >= WX_BUF - 1) break;
        }
        body[total] = 0;
    } else {
        ESP_LOGW(TAG, "HTTP open failed");
    }
    if (cli) {
        if (opened) esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
    }

    s_state = WX_FAIL;
    if (total > 0) {
        const char *cur = strstr(body, "\"current\":");   // 定位 current 段(避开 current_units)
        const char *day = strstr(body, "\"daily\":");
        if (cur) {
            s_temp_i = (int)lroundf(json_num(cur, "temperature_2m", 0));
            s_hum_i  = (int)lroundf(json_num(cur, "relative_humidity_2m", 0));
            s_code   = (int)json_num(cur, "weather_code", -1);
            s_hi     = (int)lroundf(json_num(day, "temperature_2m_max", 0));
            s_lo     = (int)lroundf(json_num(day, "temperature_2m_min", 0));
            s_is_day = json_num(cur, "is_day", 1) != 0;
            ESP_LOGI(TAG, "%s: %dC (%d/%d) code=%d hum=%d", CITY, s_temp_i, s_lo, s_hi, s_code, s_hum_i);
            ++s_revision;                         // 成功→加载→成功可短于一次 UI tick。
            s_state = WX_OK;
        }
    }
    free(body);
    s_task_alive = false;
    vTaskDelete(NULL);
}

static void start_fetch(void) {
    if (s_task_alive) return;                         // 已有拉取任务在跑 → 不重复起(单实例,自删后才允许下一次)
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) { s_state = WX_FAIL; return; }
    s_state = WX_LOADING;
    s_task_alive = true;                              // 置位在 create 前,杜绝竞态重入
    if (xTaskCreate(wx_task, "wx", 8192, NULL, 5, NULL) != pdPASS) { s_task_alive = false; s_state = WX_FAIL; }
}

// 给天气表盘用:后台按需拉取(OK 后 20 分钟刷新,否则 1 分钟重试)+ 取缓存
void weather_poll(void) {
    if (s_state == WX_LOADING) return;
    static uint32_t last = 0;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t period = (s_state == WX_OK) ? 20u * 60 * 1000 : 60u * 1000;
    if (last && (now - last) < period) return;
    last = now;
    start_fetch();
}
bool weather_cached(int *temp, int *lo, int *hi, int *code, int *hum) {
    if (s_state != WX_OK) return false;
    if (temp) *temp = s_temp_i;
    if (lo)   *lo = s_lo;
    if (hi)   *hi = s_hi;
    if (code) *code = s_code;
    if (hum)  *hum = s_hum_i;
    return true;
}

/* ===== App 生命周期 ===== */
static void weather_enter(lv_obj_t *parent) {
    launcher_set_title(CITY);
    s_shown = (wx_state_t)-1;                 // 重入必须回放当前状态
    s_shown_revision = UINT32_MAX;
    weather_ui_create(&s_ui, parent);
    start_fetch();
}

static void weather_tick(void) {
    if (!s_ui.status || (s_state == s_shown && s_revision == s_shown_revision)) return;
    s_shown = s_state;
    s_shown_revision = s_revision;
    if (s_state == WX_OK) {
        weather_ui_show(&s_ui, s_temp_i, s_lo, s_hi, s_code, s_hum_i, s_is_day);
    } else {
        weather_ui_status(&s_ui, s_state == WX_LOADING);
    }
}

static void weather_exit(void) { memset(&s_ui, 0, sizeof s_ui); }

const app_t app_weather = { "weather", COL_TXT, weather_enter, weather_tick, weather_exit };
