// OTA 升级 —— esp_https_ota 从固定 URL 拉固件刷写,带进度%;成功后重启。需先连 WiFi。
// 线程:OTA 在独立任务里跑(阻塞、联网),只写 s_state/s_pct;UI 在 ota_tick(LVGL 任务)里读。
// 安全:开了 bootloader 回滚(sdkconfig CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE)——
//   新固件启动后由 main.c 调 esp_ota_mark_app_valid_cancel_rollback() 确认;若新固件启动即崩,
//   下次复位 bootloader 自动回退旧分区。dual-OTA 分区(ota_0/ota_1)刷到另一个 slot,失败不毁当前固件。
#include "app.h"
#include "settings.h"
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
// 双通道:stable=正式(v1.6 tag),beta=内测(v1.6-beta.1 tag)。CI 规则:正式 tag 两个对象都覆盖
// (正式对内测用户也是"最新"),beta tag 只覆盖 beta 对象 → 设备只需按开关二选一,无需比较版本新旧。
#define OTA_URL_STABLE "https://ota.miaozong.cc/GeekTool.bin"
#define OTA_URL_BETA   "https://ota.miaozong.cc/GeekTool-beta.bin"

// CHECKING=连上读镜像头比版本;UPTODATE=远端与当前同版本,不刷。
typedef enum { OTA_IDLE, OTA_CHECKING, OTA_RUNNING, OTA_OK, OTA_FAIL, OTA_UPTODATE } ota_state_t;
static volatile ota_state_t s_state = OTA_IDLE;
static volatile int         s_pct = 0;         // 下载进度 0-100
static volatile bool        s_task_alive = false;
static char                 s_newver[32];      // 远端固件版本号(读镜像头得到,展示用)

static lv_obj_t *g_status, *g_bar, *g_pctlbl;

/* 带进度的 OTA:begin → 循环 perform(每次读一块)→ finish。进度 = 已读/总大小。 */
static void ota_task(void *arg) {
    // 下载期间关调制解调器睡眠拉满网速(平时 MAX_MODEM 省电但吞吐掉一截,1.8MB 包体感明显);
    // 任务结束恢复省电档。HTTP 缓冲 512→4KB,减少 TLS 分段读次数。
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_http_client_config_t http = {
        .url               = settings_beta() ? OTA_URL_BETA : OTA_URL_STABLE,
        .crt_bundle_attach = esp_crt_bundle_attach,   // HTTPS 用;HTTP 时忽略
        .timeout_ms        = 15000,
        .keep_alive_enable = true,
        .buffer_size       = 4096,
        .buffer_size_tx    = 2048,
    };
    esp_https_ota_config_t cfg = { .http_config = &http };

    esp_https_ota_handle_t h = NULL;
    esp_err_t err = esp_https_ota_begin(&cfg, &h);
    if (err != ESP_OK) { ESP_LOGE(TAG, "begin: %s", esp_err_to_name(err)); goto done; }

    // 版本比对:只读镜像头拿到新固件版本号(不下载整包),与当前运行版本比。
    // 相同 → abort 不刷,提示"已是最新";不同才继续下载。get_img_desc 失败则跳过检查照常刷(安全兜底)。
    esp_app_desc_t nd;
    if (esp_https_ota_get_img_desc(h, &nd) == ESP_OK) {
        strncpy(s_newver, nd.version, sizeof s_newver - 1); s_newver[sizeof s_newver - 1] = 0;
        const esp_app_desc_t *cur = esp_app_get_description();
        ESP_LOGI(TAG, "remote=%s current=%s", nd.version, cur->version);
        if (strncmp(nd.version, cur->version, sizeof nd.version) == 0) {
            esp_https_ota_abort(h); h = NULL;
            esp_wifi_set_ps(WIFI_PS_MAX_MODEM);        // 早退路径同样恢复省电档
            s_state = OTA_UPTODATE;                    // 同版本:不刷不重启
            s_task_alive = false;
            vTaskDelete(NULL);
            return;
        }
    }
    s_state = OTA_RUNNING;                             // 有新版 → 进入下载态(tick 显示进度)

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
    esp_wifi_set_ps(WIFI_PS_MAX_MODEM);               // 恢复省电档(成功路径马上重启,无所谓)
    ESP_LOGI(TAG, "OTA -> %s", esp_err_to_name(err));
    s_state = (err == ESP_OK) ? OTA_OK : OTA_FAIL;
    s_task_alive = false;
    if (err == ESP_OK) { vTaskDelay(pdMS_TO_TICKS(1200)); esp_restart(); }
    vTaskDelete(NULL);
}

static void beta_changed(lv_event_t *e) {             // 内测通道开关:开=收 beta+正式,关=只收正式
    bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    settings_set_beta(on ? 1 : 0);
    settings_save();
}

static void start_btn(lv_event_t *e) {
    if (s_task_alive) return;                         // 检查/下载任务在跑 → 忽略重复点
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {    // 没连 WiFi
        lv_label_set_text(g_status, tr(S_CONNECT_WIFI));
        lv_obj_set_style_text_color(g_status, lv_color_hex(COL_RED), 0);
        return;
    }
    s_pct = 0;
    lv_bar_set_value(g_bar, 0, LV_ANIM_OFF);          // 重置上次的进度条/百分比(尤其重新检查时)
    lv_label_set_text(g_pctlbl, "");
    s_state = OTA_CHECKING;                           // 先连上比版本,再决定刷不刷
    s_task_alive = true;                              // 置位在 create 前,杜绝竞态重入
    if (xTaskCreate(ota_task, "ota", 8192, NULL, 5, NULL) != pdPASS) { s_task_alive = false; s_state = OTA_FAIL; }
}

static void ota_enter(lv_obj_t *parent) {
    s_state = OTA_IDLE; s_pct = 0;

    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, tr(S_OTA_TITLE));
    lv_obj_set_style_text_font(title, UI_FONT_L, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COL_TXT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 96);

    // 当前固件版本(esp_app_desc 里的 project_version;默认取 git describe / CMake 版本)
    lv_obj_t *ver = lv_label_create(parent);
    const esp_app_desc_t *d = esp_app_get_description();
    char vb[48]; snprintf(vb, sizeof vb, "%s  %s", tr(S_CURRENT), d->version);
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
    lv_label_set_text(g_status, tr(S_TAP_UPDATE));
    lv_obj_set_style_text_color(g_status, lv_color_hex(COL_TXT2), 0);
    lv_obj_align(g_status, LV_ALIGN_CENTER, 0, 52);

    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 180, 56);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COL_RED), 0);   // 红色 CTA(唯一强调色)
    lv_obj_set_style_radius(btn, 28, 0);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -104);
    lv_obj_add_event_cb(btn, start_btn, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, "update");
    lv_obj_set_style_text_font(bl, UI_FONT_SYM, 0);
    lv_obj_set_style_text_color(bl, lv_color_hex(COL_TXT), 0);
    lv_obj_center(bl);

    // 内测通道开关(update 按钮下方一行):关=只收正式,开=beta(正式也会推 beta 通道,不漏更新)
    lv_obj_t *bt = lv_label_create(parent);
    lv_obj_set_style_text_font(bt, UI_FONT_M, 0);
    lv_obj_set_style_text_color(bt, lv_color_hex(COL_TXT2), 0);
    lv_label_set_text(bt, tr(S_BETA_CH));
    lv_obj_align(bt, LV_ALIGN_BOTTOM_MID, -44, -56);
    lv_obj_t *sw = lv_switch_create(parent);
    lv_obj_set_size(sw, 52, 28);
    lv_obj_align(sw, LV_ALIGN_BOTTOM_MID, 62, -52);
    lv_obj_remove_flag(sw, LV_OBJ_FLAG_GESTURE_BUBBLE);
    if (settings_beta()) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, beta_changed, LV_EVENT_VALUE_CHANGED, NULL);
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
        case OTA_CHECKING: lv_label_set_text(g_status, tr(S_CHECKING));
                           lv_obj_set_style_text_color(g_status, lv_color_hex(COL_TXT2), 0); break;
        case OTA_RUNNING:  lv_label_set_text(g_status, tr(S_UPDATING));
                           lv_obj_set_style_text_color(g_status, lv_color_hex(COL_TXT), 0); break;
        case OTA_OK:       lv_bar_set_value(g_bar, 100, LV_ANIM_OFF); lv_label_set_text(g_pctlbl, "100%");
                           lv_label_set_text(g_status, tr(S_DONE_REBOOT));
                           lv_obj_set_style_text_color(g_status, lv_color_hex(COL_CHARGE), 0); break;
        case OTA_UPTODATE: { char b[64]; snprintf(b, sizeof b, "%s  %s", tr(S_UPTODATE), s_newver);
                           lv_label_set_text(g_status, b);
                           lv_obj_set_style_text_color(g_status, lv_color_hex(COL_CHARGE), 0); } break;
        case OTA_FAIL:     lv_label_set_text(g_status, tr(S_FAILED));
                           lv_obj_set_style_text_color(g_status, lv_color_hex(COL_RED), 0); break;
        default: break;
    }
}

static void ota_exit(void) { g_status = g_bar = g_pctlbl = NULL; }   // 任务若在跑,自删后不再碰这些指针(tick 已停)

const app_t app_ota = { "ota", COL_TXT, ota_enter, ota_tick, ota_exit };
