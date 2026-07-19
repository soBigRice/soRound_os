// OTA 升级 —— esp_https_ota 从固定 URL 拉固件刷写,带进度%;成功后重启。需先连 WiFi。
// 线程:OTA 在独立任务里跑(阻塞、联网),只写 s_state/s_pct;UI 在 ota_tick(LVGL 任务)里读。
// 安全:开了 bootloader 回滚(sdkconfig CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE)——
//   新固件启动后由 main.c 调 esp_ota_mark_app_valid_cancel_rollback() 确认;若新固件启动即崩,
//   下次复位 bootloader 自动回退旧分区。dual-OTA 分区(ota_0/ota_1)刷到另一个 slot,失败不毁当前固件。
#include "app.h"
#include "settings.h"
#include "glyph.h"
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
static int                  s_last_pct = -1;   // 每次进页面重放当前任务进度
static ota_state_t          s_shown = (ota_state_t)-1;

static lv_obj_t *g_status, *g_ver, *g_ring, *g_icon, *g_pctlbl;

/* ===== 圆屏视觉:大进度环 + 中心手绘图标(下载箭头弹跳 / 对勾 / 叉) ===== */
#define IC_CX 60           // g_icon 容器 120x120,中心 (60,60)
#define IC_CY 60

static void draw_dl_arrow(uint32_t col) {          // 下载箭头:竖杆 + 箭头 + 底托
    glyph_line(g_icon, IC_CX, IC_CY - 34, IC_CX, IC_CY + 14, 8, 3, col);       // 竖杆
    glyph_line(g_icon, IC_CX, IC_CY + 16, IC_CX - 22, IC_CY - 8, 8, 3, col);   // 左斜
    glyph_line(g_icon, IC_CX, IC_CY + 16, IC_CX + 22, IC_CY - 8, 8, 3, col);   // 右斜
    glyph_line(g_icon, IC_CX - 28, IC_CY + 34, IC_CX + 28, IC_CY + 34, 9, 3, col); // 底托
}
static void draw_check(uint32_t col) {             // 对勾
    glyph_line(g_icon, IC_CX - 26, IC_CY, IC_CX - 6, IC_CY + 22, 7, 3, col);
    glyph_line(g_icon, IC_CX - 6, IC_CY + 22, IC_CX + 28, IC_CY - 20, 7, 3, col);
}
static void draw_cross(uint32_t col) {             // 叉
    glyph_line(g_icon, IC_CX - 22, IC_CY - 22, IC_CX + 22, IC_CY + 22, 7, 3, col);
    glyph_line(g_icon, IC_CX + 22, IC_CY - 22, IC_CX - 22, IC_CY + 22, 7, 3, col);
}
static void set_icon(int kind, uint32_t col) {     // 0=箭头 1=对勾 2=叉 3=空(下载显数字)
    lv_obj_clean(g_icon);
    if (kind == 0) draw_dl_arrow(col);
    else if (kind == 1) draw_check(col);
    else if (kind == 2) draw_cross(col);
}

// 中心图标上下弹跳(下载/待命时);终态停止归位
static void bob_exec(void *o, int32_t v) { lv_obj_set_style_translate_y((lv_obj_t *)o, v, 0); }
static void start_bob(void) {
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, g_icon);
    lv_anim_set_exec_cb(&a, bob_exec);
    lv_anim_set_values(&a, -7, 9);
    lv_anim_set_duration(&a, 620);
    lv_anim_set_playback_duration(&a, 620);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}
static void stop_bob(void) { lv_anim_delete(g_icon, bob_exec); if (g_icon) lv_obj_set_style_translate_y(g_icon, 0, 0); }

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
    s_last_pct = -1;
    lv_arc_set_value(g_ring, 0);                      // 重置进度环 + 百分比(尤其重新检查时)
    lv_label_set_text(g_pctlbl, "");
    s_state = OTA_CHECKING;                           // 先连上比版本,再决定刷不刷
    s_task_alive = true;                              // 置位在 create 前,杜绝竞态重入
    if (xTaskCreate(ota_task, "ota", 8192, NULL, 5, NULL) != pdPASS) { s_task_alive = false; s_state = OTA_FAIL; }
}

static void ota_enter(lv_obj_t *parent) {
    // OTA task 可以在离开页面后继续。这里不能重置 s_state/s_pct,否则重进时会显示 idle,
    // 但 start_btn 又因 s_task_alive 拒绝点击,形成“后台在下、前台像卡住”的假状态。
    // 只重置 UI 去重缓存,让首个 tick 把当前后台状态完整重放到新控件。
    s_last_pct = -1;
    s_shown = (ota_state_t)-1;

    // 当前版本(顶部小字)
    g_ver = lv_label_create(parent);
    const esp_app_desc_t *d = esp_app_get_description();
    char vb[48]; snprintf(vb, sizeof vb, "%s  %s", tr(S_CURRENT), d->version);
    lv_label_set_text(g_ver, vb);
    lv_obj_set_style_text_font(g_ver, UI_FONT_M, 0);
    lv_obj_set_style_text_color(g_ver, lv_color_hex(COL_TXT2), 0);
    lv_obj_align(g_ver, LV_ALIGN_TOP_MID, 0, 96);

    // ★大进度环(圆屏主角):暗底环 + 红进度,居中略偏上
    g_ring = lv_arc_create(parent);
    lv_obj_set_size(g_ring, 268, 268);
    lv_obj_align(g_ring, LV_ALIGN_CENTER, 0, -18);
    lv_arc_set_rotation(g_ring, 270);
    lv_arc_set_bg_angles(g_ring, 0, 360);
    lv_arc_set_range(g_ring, 0, 100);
    lv_arc_set_value(g_ring, 0);
    lv_obj_remove_style(g_ring, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(g_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(g_ring, lv_color_hex(0x1c1c22), LV_PART_MAIN);
    lv_obj_set_style_arc_width(g_ring, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_color(g_ring, lv_color_hex(COL_RED), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(g_ring, 10, LV_PART_INDICATOR);

    // 中心手绘图标容器(下载箭头 / 对勾 / 叉)—— 弹跳动画作用于它
    g_icon = lv_obj_create(parent);
    lv_obj_remove_style_all(g_icon);
    lv_obj_set_size(g_icon, 120, 120);
    lv_obj_align_to(g_icon, g_ring, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(g_icon, LV_OBJ_FLAG_EVENT_BUBBLE);

    // 中心大百分比(下载时显示,盖住箭头)
    g_pctlbl = lv_label_create(parent);
    lv_obj_set_style_text_font(g_pctlbl, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(g_pctlbl, lv_color_hex(COL_TXT), 0);
    lv_label_set_text(g_pctlbl, "");
    lv_obj_align_to(g_pctlbl, g_ring, LV_ALIGN_CENTER, 0, 0);

    // 状态文字(环下方)
    g_status = lv_label_create(parent);
    lv_label_set_long_mode(g_status, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(g_status, 300);
    lv_obj_set_style_text_align(g_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_status, UI_FONT_M, 0);
    lv_label_set_text(g_status, tr(S_TAP_UPDATE));
    lv_obj_set_style_text_color(g_status, lv_color_hex(COL_TXT2), 0);
    lv_obj_align(g_status, LV_ALIGN_BOTTOM_MID, 0, -86);

    // ★点环即更新:环中心一块透明圆形点击区(不再有独立红按钮,更契合圆屏)
    lv_obj_t *hit = lv_obj_create(parent);
    lv_obj_remove_style_all(hit);
    lv_obj_set_size(hit, 220, 220);
    lv_obj_align_to(hit, g_ring, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(hit, LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(hit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(hit, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(hit, start_btn, LV_EVENT_CLICKED, NULL);

    // 内测通道开关(底部一行)
    lv_obj_t *bt = lv_label_create(parent);
    lv_obj_set_style_text_font(bt, UI_FONT_M, 0);
    lv_obj_set_style_text_color(bt, lv_color_hex(COL_TXT2), 0);
    lv_label_set_text(bt, tr(S_BETA_CH));
    lv_obj_align(bt, LV_ALIGN_BOTTOM_MID, -44, -44);
    lv_obj_t *sw = lv_switch_create(parent);
    lv_obj_set_size(sw, 52, 28);
    lv_obj_align(sw, LV_ALIGN_BOTTOM_MID, 62, -40);
    lv_obj_remove_flag(sw, LV_OBJ_FLAG_GESTURE_BUBBLE);
    if (settings_beta()) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, beta_changed, LV_EVENT_VALUE_CHANGED, NULL);

    set_icon(0, COL_TXT);      // 待命:下载箭头
    start_bob();               // 弹跳邀请点击
}

static void ota_tick(void) {
    if (!g_status) return;
    if (s_state == OTA_RUNNING && s_pct != s_last_pct) {   // 环进度 + 中心大百分比
        s_last_pct = s_pct;
        lv_arc_set_value(g_ring, s_pct);
        char pb[8]; snprintf(pb, sizeof pb, "%d", s_pct);
        lv_label_set_text(g_pctlbl, pb);
    }
    if (s_state == s_shown) return;
    s_shown = s_state;
    switch (s_state) {
        case OTA_CHECKING: lv_label_set_text(g_status, tr(S_CHECKING));
                           lv_obj_set_style_text_color(g_status, lv_color_hex(COL_TXT2), 0); break;
        case OTA_RUNNING:  stop_bob(); set_icon(3, 0);                 // 下载:箭头让位给中心大数字
                           lv_label_set_text(g_status, tr(S_UPDATING));
                           lv_obj_set_style_text_color(g_status, lv_color_hex(COL_TXT), 0); break;
        case OTA_OK:       stop_bob(); lv_arc_set_value(g_ring, 100); lv_label_set_text(g_pctlbl, "");
                           set_icon(1, COL_CHARGE);                    // 对勾(绿)
                           lv_label_set_text(g_status, tr(S_DONE_REBOOT));
                           lv_obj_set_style_text_color(g_status, lv_color_hex(COL_CHARGE), 0); break;
        case OTA_UPTODATE: { stop_bob(); lv_arc_set_value(g_ring, 100); lv_label_set_text(g_pctlbl, "");
                           set_icon(1, COL_CHARGE);                    // 对勾(绿):已是最新
                           char b[64]; snprintf(b, sizeof b, "%s  %s", tr(S_UPTODATE), s_newver);
                           lv_label_set_text(g_status, b);
                           lv_obj_set_style_text_color(g_status, lv_color_hex(COL_CHARGE), 0); } break;
        case OTA_FAIL:     stop_bob(); lv_arc_set_value(g_ring, 0); lv_label_set_text(g_pctlbl, "");
                           set_icon(2, COL_RED);                       // 叉(红)
                           lv_label_set_text(g_status, tr(S_FAILED));
                           lv_obj_set_style_text_color(g_status, lv_color_hex(COL_RED), 0); break;
        default: break;
    }
}

static void ota_exit(void) {
    stop_bob();                                          // 停动画再清指针(tick 已停,任务在跑也只碰 s_state/s_pct)
    g_status = g_ver = g_ring = g_icon = g_pctlbl = NULL;
}

const app_t app_ota = { "ota", COL_TXT, ota_enter, ota_tick, ota_exit };
