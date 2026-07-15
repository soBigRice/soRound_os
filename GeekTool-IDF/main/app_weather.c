// 天气 app —— Open-Meteo 拉上海实时天气(JSON 数值 + WMO code,带当日低/高温),Nothing 点描风。
// 图标/温度都用 glyph_* 沿轮廓撒点。线程:HTTP 在独立任务里阻塞拉取,只写 s_*;UI 在 weather_tick 更新。
#include "app.h"
#include "glyph.h"
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
                "&current=temperature_2m,relative_humidity_2m,weather_code" \
                "&daily=temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=1"
#define WX_BUF  8192                                   // open-meteo 响应 ~1-2KB

typedef enum { WX_IDLE, WX_LOADING, WX_OK, WX_FAIL } wx_state_t;
static volatile wx_state_t s_state = WX_IDLE;
static volatile bool       s_task_alive = false;   // 拉取任务在跑?防反复进 app 起多个 8KB 栈任务堆积爆内存
static int  s_temp_i = 0, s_lo = 0, s_hi = 0, s_hum_i = 0, s_code = -1;
static char s_cond[40] = "";

static lv_obj_t *g_iconbox, *g_temp, *g_cond, *g_range, *g_status;

/* ===== 天气图标(点描,统一 dr3/step10 轮廓;标准形 + 单红点缀)===== */
#define IC_DR   3
#define IC_ST   10

// 云:中大瓣 + 左右对称小瓣 + 平底,只描外轮廓(剔除被邻瓣/底线盖住的内部点)。对称 → 标准云形
static void draw_cloud(lv_obj_t *p, int cx, int cy, uint32_t col) {
    struct { float x, y, r; } lobe[3] = { { cx - 40, cy + 8, 26 }, { cx, cy - 14, 36 }, { cx + 40, cy + 8, 26 } };
    float yb = cy + 32;
    int   xL = cx - 58, xR = cx + 58;
    for (int k = 0; k < 3; k++) {
        int nn = (int)(2 * 3.14159f * lobe[k].r / IC_ST);
        for (int i = 0; i < nn; i++) {
            float a = (float)i / nn * 6.28318f;
            float X = lobe[k].x + cosf(a) * lobe[k].r, Y = lobe[k].y + sinf(a) * lobe[k].r;
            if (Y > yb) continue;                                    // 底线以下留给平底
            bool ins = false;
            for (int j = 0; j < 3; j++) {
                if (j == k) continue;
                float dx = X - lobe[j].x, dy = Y - lobe[j].y, rr = lobe[j].r - 2;
                if (dx * dx + dy * dy < rr * rr) { ins = true; break; }
            }
            if (!ins) glyph_dot(p, (int)X, (int)Y, IC_DR, col);
        }
    }
    int bn = (xR - xL) / IC_ST;                                      // 平底
    for (int i = 0; i <= bn; i++) glyph_dot(p, xL + (xR - xL) * i / bn, (int)yb, IC_DR, col);
}
// 晴:干净描边圆 + 8 条直射线(标准太阳,去掉红核)
static void draw_sun(lv_obj_t *p, int cx, int cy) {
    glyph_circle(p, cx, cy, 27, IC_ST, IC_DR, COL_TXT);
    for (int k = 0; k < 8; k++) {
        float a = k * 3.14159f / 4, c = cosf(a), s = sinf(a);
        glyph_line(p, cx + (int)(c * 37), cy + (int)(s * 37), cx + (int)(c * 52), cy + (int)(s * 52), 7, IC_DR, COL_TXT);
    }
}
// 雨:云 + 3 道等距斜雨(中间一道红,单点缀)
static void draw_rain(lv_obj_t *p, int cx, int cy) {
    draw_cloud(p, cx, cy - 12, COL_TXT);
    for (int i = -1; i <= 1; i++)
        glyph_line(p, cx + i * 30 + 8, cy + 34, cx + i * 30 - 2, cy + 58, 9, IC_DR, i == 0 ? COL_RED : COL_TXT);
}
// 雪:云 + 3 片六角雪花(每片=过中心 3 条短线 → 6 角星)
static void draw_snow(lv_obj_t *p, int cx, int cy) {
    draw_cloud(p, cx, cy - 12, COL_TXT);
    int fx[3] = { cx - 30, cx, cx + 30 }, fy[3] = { cy + 46, cy + 54, cy + 46 };
    for (int f = 0; f < 3; f++)
        for (int k = 0; k < 3; k++) {
            float a = k * 3.14159f / 3, c = cosf(a) * 9, s = sinf(a) * 9;
            glyph_line(p, fx[f] - (int)c, fy[f] - (int)s, fx[f] + (int)c, fy[f] + (int)s, 6, 2, COL_TXT);
        }
}
static void draw_wicon(int code) {           // WMO weather code → 点描图标
    lv_obj_clean(g_iconbox);
    int cx = 110, cy = 80;
    if      (code <= 1)                                              draw_sun(g_iconbox, cx, cy);           // 0,1 晴
    else if ((code >= 71 && code <= 77) || code == 85 || code == 86) draw_snow(g_iconbox, cx, cy);          // 雪
    else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82) || code >= 95) draw_rain(g_iconbox, cx, cy); // 雨/雷
    else                                                            draw_cloud(g_iconbox, cx, cy, COL_TXT); // 2,3,45,48 多云/雾
}

/* ===== 大号点阵温度(5×7,字模用 glyph_font5x7[])===== */
#define TP 13
#define TDR 4
static void draw_temp(int t) {
    lv_obj_clean(g_temp);
    char s[16]; snprintf(s, sizeof s, "%d", t < 0 ? -t : t);
    int n = strlen(s), dw = 5 * TP, gap = TP, x = 0;
    if (t < 0) { for (int c = 0; c < 5; c++) glyph_dot(g_temp, x + c * TP + TP / 2, 3 * TP + TP / 2, TDR, COL_TXT); x += dw + gap; }
    for (int k = 0; k < n; k++) {
        const char *const *g = glyph_font5x7[s[k] - '0'];
        for (int r = 0; r < 7; r++)
            for (int c = 0; c < 5; c++)
                if (g[r][c] == '1') glyph_dot(g_temp, x + c * TP + TP / 2, r * TP + TP / 2, TDR, COL_TXT);
        x += dw + gap;
    }
    lv_obj_t *deg = lv_obj_create(g_temp);          // 度环 °
    lv_obj_remove_style_all(deg);
    lv_obj_set_size(deg, 20, 20);
    lv_obj_set_pos(deg, x, 2);
    lv_obj_set_style_radius(deg, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(deg, 4, 0);
    lv_obj_set_style_border_color(deg, lv_color_hex(COL_TXT), 0);
    lv_obj_set_style_border_opa(deg, LV_OPA_COVER, 0);
    x += 28;
    lv_obj_set_size(g_temp, x, 7 * TP);
    lv_obj_align(g_temp, LV_ALIGN_TOP_MID, 0, 205);
}

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
// WMO weather code → 文字
static const char *wmo_desc(int c) {
    if (c == 0)  return "Clear";
    if (c <= 2)  return "Partly cloudy";
    if (c == 3)  return "Overcast";
    if (c <= 48) return "Fog";
    if (c <= 57) return "Drizzle";
    if (c <= 67) return "Rain";
    if (c <= 77) return "Snow";
    if (c <= 82) return "Rain showers";
    if (c <= 86) return "Snow showers";
    return "Thunderstorm";
}

static void wx_task(void *arg) {
    esp_http_client_config_t cfg = {
        .url = WX_URL, .crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = 12000,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    char *body = heap_caps_malloc(WX_BUF, MALLOC_CAP_SPIRAM);   // 大缓冲放 PSRAM
    if (!body) body = malloc(WX_BUF);
    int total = 0;
    if (body && esp_http_client_open(cli, 0) == ESP_OK) {
        esp_http_client_fetch_headers(cli);
        int nb;
        while ((nb = esp_http_client_read(cli, body + total, WX_BUF - 1 - total)) > 0) {
            total += nb;
            if (total >= WX_BUF - 1) break;
        }
        body[total] = 0;
    }
    esp_http_client_close(cli);
    esp_http_client_cleanup(cli);

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
            strncpy(s_cond, wmo_desc(s_code), sizeof s_cond - 1); s_cond[sizeof s_cond - 1] = 0;
            ESP_LOGI(TAG, "%s: %dC (%d/%d) code=%d hum=%d", CITY, s_temp_i, s_lo, s_hi, s_code, s_hum_i);
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
static lv_obj_t *mklabel(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int y) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, "");
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    return l;
}

static void weather_enter(lv_obj_t *parent) {
    launcher_set_title(CITY);                 // 顶部标题显示城市

    g_iconbox = lv_obj_create(parent);
    lv_obj_remove_style_all(g_iconbox);
    lv_obj_set_size(g_iconbox, 220, 160);
    lv_obj_align(g_iconbox, LV_ALIGN_TOP_MID, 0, 72);

    g_temp = lv_obj_create(parent);
    lv_obj_remove_style_all(g_temp);
    lv_obj_set_size(g_temp, 10, 10);
    lv_obj_align(g_temp, LV_ALIGN_TOP_MID, 0, 205);

    g_cond   = mklabel(parent, UI_FONT_L, COL_TXT,  312);
    g_range  = mklabel(parent, UI_FONT_M, COL_TXT2, 344);
    g_status = mklabel(parent, UI_FONT_M, COL_TXT2, 376);
    lv_label_set_text(g_status, "loading...");

    start_fetch();
}

static void weather_tick(void) {
    static wx_state_t shown = WX_IDLE;
    if (!g_status || s_state == shown) return;
    shown = s_state;
    if (s_state == WX_OK) {
        draw_wicon(s_code);
        draw_temp(s_temp_i);
        lv_label_set_text(g_cond, s_cond);
        char r[64]; snprintf(r, sizeof r, "%d / %d   hum %d%%", s_lo, s_hi, s_hum_i);
        lv_label_set_text(g_range, r);
        lv_label_set_text(g_status, "");
    } else if (s_state == WX_LOADING) {
        lv_label_set_text(g_status, "loading...");
    } else {
        lv_label_set_text(g_status, "no wifi / fetch failed");
        lv_obj_set_style_text_color(g_status, lv_color_hex(COL_RED), 0);
    }
}

static void weather_exit(void) { g_iconbox = g_temp = g_cond = g_range = g_status = NULL; }

const app_t app_weather = { "weather", COL_TXT, weather_enter, weather_tick, weather_exit };
