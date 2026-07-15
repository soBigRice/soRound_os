// OTA 升级 —— esp_https_ota 从固定 URL 拉固件刷写,带进度%;成功后重启。需先连 WiFi。
// 线程:OTA 在独立任务里跑(阻塞、联网),只写 s_state/s_pct;UI 在 ota_tick(LVGL 任务)里读。
// 安全:开了 bootloader 回滚(sdkconfig CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE)——
//   新固件启动后由 main.c 调 esp_ota_mark_app_valid_cancel_rollback() 确认;若新固件启动即崩,
//   下次复位 bootloader 自动回退旧分区。dual-OTA 分区(ota_0/ota_1)刷到另一个 slot,失败不毁当前固件。
#include "app.h"
#include "esp_wifi.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ota";

// 云 OTA:Cloudflare R2 自定义域名直链(国内可达,GitHub release 资产国内常被墙)。
// 打 v* tag → Actions 构建 → 传 GeekTool.bin 到 R2 bucket(覆盖同名对象)→ 设备点 update 即拉最新。
//   直链无跳转、无鉴权、HTTPS(Cloudflare 证书在 FULL crt_bundle 里)。
//   上传时带 Cache-Control:no-store,边缘不缓存 → 永远拉最新(CI 那步已设,无需后台缓存规则)。
//   本地测试想用局域网 HTTP,临时改回 http://<你电脑IP>:8000/GeekTool.bin(build 目录起 http.server)。
#define OTA_URL "https://ota.miaozong.cc/GeekTool.bin"

typedef enum { OTA_IDLE, OTA_RUNNING, OTA_OK, OTA_FAIL } ota_state_t;
static volatile ota_state_t s_state = OTA_IDLE;
static volatile int         s_pct = 0;         // 下载进度 0-100
static volatile bool        s_task_alive = false;

static lv_obj_t *g_status, *g_bar, *g_pctlbl;

/* 带进度的 OTA:begin → 循环 perform(每次读一块)→ finish。进度 = 已读/总大小。 */
static void ota_task(void *arg) {
    esp_http_client_config_t http = {
        .url               = OTA_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,   // HTTPS 用;HTTP 时忽略
        .timeout_ms        = 15000,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t cfg = { .http_config = &http };

    esp_https_ota_handle_t h = NULL;
    esp_err_t err = esp_https_ota_begin(&cfg, &h);
    if (err != ESP_OK) { ESP_LOGE(TAG, "begin: %s", esp_err_to_name(err)); goto done; }

    int total = esp_https_ota_get_image_size(h);      // Content-Length;可能为 -1(chunked)
    while ((err = esp_https_ota_perform(h)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        int rd = esp_https_ota_get_image_len_read(h);
        s_pct = (total > 0) ? (rd * 100 / total) : 0;
    }
    if (err == ESP_OK && esp_https_ota_is_complete_data_received(h)) {
        s_pct = 100;
        err = esp_https_ota_finish(h);                // 校验 + 切 boot 分区
        h = NULL;                                     // finish 已释放句柄
    } else {
        ESP_LOGE(TAG, "perform: %s", esp_err_to_name(err));
        if (h) esp_https_ota_abort(h);
        h = NULL;
        if (err == ESP_OK) err = ESP_FAIL;            // 数据没收全也算失败
    }

done:
    ESP_LOGI(TAG, "OTA -> %s", esp_err_to_name(err));
    s_state = (err == ESP_OK) ? OTA_OK : OTA_FAIL;
    s_task_alive = false;
    if (err == ESP_OK) { vTaskDelay(pdMS_TO_TICKS(1200)); esp_restart(); }
    vTaskDelete(NULL);
}

static void start_btn(lv_event_t *e) {
    if (s_task_alive || s_state == OTA_RUNNING) return;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {    // 没连 WiFi
        lv_label_set_text(g_status, "connect wifi first");
        lv_obj_set_style_text_color(g_status, lv_color_hex(COL_RED), 0);
        return;
    }
    s_pct = 0;
    s_state = OTA_RUNNING;
    s_task_alive = true;                              // 置位在 create 前,杜绝竞态重入
    if (xTaskCreate(ota_task, "ota", 8192, NULL, 5, NULL) != pdPASS) { s_task_alive = false; s_state = OTA_FAIL; }
}

static void ota_enter(lv_obj_t *parent) {
    s_state = OTA_IDLE; s_pct = 0;

    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "OTA update");
    lv_obj_set_style_text_font(title, UI_FONT_L, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COL_TXT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 96);

    // 当前固件版本(esp_app_desc 里的 project_version;默认取 git describe / CMake 版本)
    lv_obj_t *ver = lv_label_create(parent);
    const esp_app_desc_t *d = esp_app_get_description();
    char vb[48]; snprintf(vb, sizeof vb, "current  %s", d->version);
    lv_label_set_text(ver, vb);
    lv_obj_set_style_text_font(ver, UI_FONT_M, 0);
    lv_obj_set_style_text_color(ver, lv_color_hex(COL_TXT2), 0);
    lv_obj_align(ver, LV_ALIGN_TOP_MID, 0, 132);

    // 进度条(Nothing 风:细条,红填充)——仅运行/完成时有值
    g_bar = lv_bar_create(parent);
    lv_obj_set_size(g_bar, 300, 8);
    lv_obj_align(g_bar, LV_ALIGN_CENTER, 0, -8);
    lv_obj_set_style_bg_color(g_bar, lv_color_hex(0x1c1c22), 0);
    lv_obj_set_style_bg_color(g_bar, lv_color_hex(COL_RED), LV_PART_INDICATOR);
    lv_bar_set_value(g_bar, 0, LV_ANIM_OFF);

    g_pctlbl = lv_label_create(parent);
    lv_obj_set_style_text_font(g_pctlbl, UI_FONT_M, 0);
    lv_obj_set_style_text_color(g_pctlbl, lv_color_hex(COL_TXT), 0);
    lv_label_set_text(g_pctlbl, "");
    lv_obj_align(g_pctlbl, LV_ALIGN_CENTER, 0, 20);

    g_status = lv_label_create(parent);
    lv_label_set_long_mode(g_status, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(g_status, 340);
    lv_obj_set_style_text_align(g_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_status, UI_FONT_M, 0);
    lv_label_set_text(g_status, "tap update to flash from server");
    lv_obj_set_style_text_color(g_status, lv_color_hex(COL_TXT2), 0);
    lv_obj_align(g_status, LV_ALIGN_CENTER, 0, 52);

    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 180, 56);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COL_RED), 0);   // 红色 CTA(唯一强调色)
    lv_obj_set_style_radius(btn, 28, 0);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -84);
    lv_obj_add_event_cb(btn, start_btn, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, "update");
    lv_obj_set_style_text_font(bl, UI_FONT_SYM, 0);
    lv_obj_set_style_text_color(bl, lv_color_hex(COL_TXT), 0);
    lv_obj_center(bl);
}

static void ota_tick(void) {
    if (!g_status) return;
    static int last_pct = -1;
    if (s_state == OTA_RUNNING && s_pct != last_pct) {   // 刷进度条(每 % 变化才动)
        last_pct = s_pct;
        lv_bar_set_value(g_bar, s_pct, LV_ANIM_OFF);
        char pb[8]; snprintf(pb, sizeof pb, "%d%%", s_pct);
        lv_label_set_text(g_pctlbl, pb);
    }
    static ota_state_t shown = -1;
    if (s_state == shown) return;
    shown = s_state;
    switch (s_state) {
        case OTA_RUNNING: lv_label_set_text(g_status, "updating - do not power off");
                          lv_obj_set_style_text_color(g_status, lv_color_hex(COL_TXT), 0); break;
        case OTA_OK:      lv_bar_set_value(g_bar, 100, LV_ANIM_OFF); lv_label_set_text(g_pctlbl, "100%");
                          lv_label_set_text(g_status, "done - rebooting");
                          lv_obj_set_style_text_color(g_status, lv_color_hex(COL_CHARGE), 0); break;
        case OTA_FAIL:    lv_label_set_text(g_status, "failed - check url / server");
                          lv_obj_set_style_text_color(g_status, lv_color_hex(COL_RED), 0); break;
        default: break;
    }
}

static void ota_exit(void) { g_status = g_bar = g_pctlbl = NULL; }   // 任务若在跑,自删后不再碰这些指针(tick 已停)

const app_t app_ota = { "ota", COL_TXT, ota_enter, ota_tick, ota_exit };
