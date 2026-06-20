// OTA 升级 —— esp_https_ota 从固定 URL 拉固件刷写,成功后重启。需先连 WiFi。
// 线程:OTA 在独立任务里跑(阻塞、联网),只改 s_state;UI 在 ota_tick(LVGL 任务)里读状态。
#include "app.h"
#include "esp_wifi.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ota";

// ★★ 改成你的固件地址。本地最省事:固件目录里 `python3 -m http.server 8000`,
//    URL 用 http://<你电脑IP>:8000/GeekTool-IDF.bin(HTTP 不需要证书)。
#define OTA_URL "http://192.168.1.100:8000/GeekTool-IDF.bin"

typedef enum { OTA_IDLE, OTA_RUNNING, OTA_OK, OTA_FAIL } ota_state_t;
static volatile ota_state_t s_state = OTA_IDLE;

static lv_obj_t *g_status;

static void ota_task(void *arg) {
    esp_http_client_config_t http = {
        .url               = OTA_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,   // HTTPS 用;HTTP 时忽略
        .timeout_ms        = 15000,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t cfg = { .http_config = &http };
    esp_err_t err = esp_https_ota(&cfg);
    ESP_LOGI(TAG, "esp_https_ota -> %s", esp_err_to_name(err));
    s_state = (err == ESP_OK) ? OTA_OK : OTA_FAIL;
    if (err == ESP_OK) { vTaskDelay(pdMS_TO_TICKS(1000)); esp_restart(); }
    vTaskDelete(NULL);
}

static void start_btn(lv_event_t *e) {
    if (s_state == OTA_RUNNING) return;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {   // 没连 WiFi
        lv_label_set_text(g_status, "connect WiFi first");
        lv_obj_set_style_text_color(g_status, lv_color_hex(COL_WARN), 0);
        return;
    }
    s_state = OTA_RUNNING;
    xTaskCreate(ota_task, "ota", 8192, NULL, 5, NULL);
}

static void ota_enter(lv_obj_t *parent) {
    s_state = OTA_IDLE;

    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "OTA Update");
    lv_obj_set_style_text_font(title, UI_FONT_L, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COL_OTA), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 120);

    g_status = lv_label_create(parent);
    lv_label_set_long_mode(g_status, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(g_status, 340);
    lv_obj_set_style_text_align(g_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(g_status, "tap update to flash from server");
    lv_obj_set_style_text_color(g_status, lv_color_hex(COL_TXT2), 0);
    lv_obj_align(g_status, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 180, 56);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COL_RED), 0);   // 红色 CTA(唯一强调色)
    lv_obj_set_style_radius(btn, 28, 0);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -118);
    lv_obj_add_event_cb(btn, start_btn, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, "Update");
    lv_obj_set_style_text_color(bl, lv_color_hex(COL_TXT), 0);
    lv_obj_center(bl);
}

static void ota_tick(void) {
    static ota_state_t shown = OTA_IDLE;
    if (!g_status || s_state == shown) return;
    shown = s_state;
    switch (s_state) {
        case OTA_RUNNING: lv_label_set_text(g_status, "updating... do not power off");
                          lv_obj_set_style_text_color(g_status, lv_color_hex(COL_WIFI), 0); break;
        case OTA_OK:      lv_label_set_text(g_status, "done, rebooting");
                          lv_obj_set_style_text_color(g_status, lv_color_hex(COL_OK), 0); break;
        case OTA_FAIL:    lv_label_set_text(g_status, "failed - check URL / server");
                          lv_obj_set_style_text_color(g_status, lv_color_hex(COL_WARN), 0); break;
        default: break;
    }
}

static void ota_exit(void) { g_status = NULL; }

const app_t app_ota = { "OTA", COL_OTA, ota_enter, ota_tick, ota_exit };
