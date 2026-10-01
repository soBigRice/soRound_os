/*
 * 数字孪生(twin)—— 通过 BLE 把 IMU/电量实时推给浏览器(Web Bluetooth),网页同步驱动 3D 模型。
 * 设备做 GATT 外设(ble_twin.c);本 app 负责 UI + 后台采样任务打包帧 + notify。
 *
 * 数据帧:20 字节,小端,须与 web/src/bluetooth.ts 的解析一致 ——
 *   [0]   ver = 0x01
 *   [1]   flags: bit0 充电 / bit1 充满
 *   [2]   soc  电量 0-100
 *   [3]   seq  帧序号(网页可检丢包)
 *   [4..9]  accel int16 ×3,单位毫g(g×1000)
 *   [10..15] gyro  int16 ×3,单位 0.1°/s(dps×10)
 *   [16..19] uptime_ms uint32
 */
#include "app.h"
#include "ble_twin.h"
#include "imu.h"
#include "ui_update.h"
#include "power.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

#define FRAME_LEN 20
#define SAMPLE_MS 33          // ~30Hz

static lv_obj_t *g_status, *g_vals, *g_dot;
static TaskHandle_t   s_task;
static portMUX_TYPE   s_mux = portMUX_INITIALIZER_UNLOCKED;
static int            s_shown_link = -1;
static bool           s_started;
static volatile bool  s_run;
static bool           s_wifi_prev;

// 采样任务写、tick 读的共享快照;短临界区保证各轴属于同一帧
static volatile int   s_soc;
static volatile float s_ax, s_ay, s_az, s_gx, s_gy, s_gz;
static volatile bool  s_linked;
static volatile uint8_t s_last_rx;
static volatile bool  s_has_rx;

static inline void put16(uint8_t *p, int16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

// 网页 → 设备:收到一字节指令(在 BLE host 任务上下文)。MVP 只记录给屏显;可扩展为震动/响铃。
static void on_rx(const uint8_t *d, size_t n) {
    if (!n) return;
    s_last_rx = d[0];
    s_has_rx  = true;
    ESP_LOGI("twin", "rx 0x%02x (%u bytes)", d[0], (unsigned)n);
}

static void sampler(void *arg) {
    (void)arg;
    imu_init();
    uint8_t seq = 0;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        portENTER_CRITICAL(&s_mux);
        bool stop = !s_run;
        if (stop) s_task = NULL;
        portEXIT_CRITICAL(&s_mux);
        if (stop) { vTaskDelete(NULL); return; }
        float ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0;
        imu_read_accel(&ax, &ay, &az);
        imu_read_gyro(&gx, &gy, &gz);
        int soc = 0; pwr_state_t st = PWR_UNKNOWN;
        power_read(&soc, &st);
        if (soc < 0) soc = 0; else if (soc > 100) soc = 100;

        bool linked = ble_twin_connected();
        portENTER_CRITICAL(&s_mux);
        s_ax = ax; s_ay = ay; s_az = az; s_gx = gx; s_gy = gy; s_gz = gz;
        s_soc = soc; s_linked = linked;
        portEXIT_CRITICAL(&s_mux);

        uint8_t f[FRAME_LEN];
        f[0] = 0x01;
        f[1] = (st == PWR_CHARGING ? 0x01 : 0) | (st == PWR_FULL ? 0x02 : 0);
        f[2] = (uint8_t)soc;
        f[3] = seq++;
        put16(&f[4],  (int16_t)(ax * 1000.0f));
        put16(&f[6],  (int16_t)(ay * 1000.0f));
        put16(&f[8],  (int16_t)(az * 1000.0f));
        put16(&f[10], (int16_t)(gx * 10.0f));
        put16(&f[12], (int16_t)(gy * 10.0f));
        put16(&f[14], (int16_t)(gz * 10.0f));
        uint32_t up = (uint32_t)(esp_timer_get_time() / 1000);
        f[16] = up; f[17] = up >> 8; f[18] = up >> 16; f[19] = up >> 24;

        ble_twin_notify(f, sizeof f);   // 未连/未订阅时静默丢弃
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(SAMPLE_MS));
    }
}

static void twin_enter(lv_obj_t *parent) {
    launcher_set_title("twin");

    g_status = lv_label_create(parent);
    lv_obj_set_style_text_font(g_status, UI_FONT_M, 0);
    lv_obj_set_style_text_color(g_status, lv_color_hex(COL_TXT), 0);
    lv_obj_set_style_text_align(g_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(g_status, "starting BLE...");
    lv_obj_align(g_status, LV_ALIGN_CENTER, 0, -70);

    // 连接指示点:未连灰 / 已连绿
    g_dot = lv_obj_create(parent);
    lv_obj_remove_style_all(g_dot);
    lv_obj_set_size(g_dot, 16, 16);
    lv_obj_set_style_radius(g_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_dot, lv_color_hex(COL_TXT2), 0);
    lv_obj_set_style_bg_opa(g_dot, LV_OPA_COVER, 0);
    lv_obj_align(g_dot, LV_ALIGN_CENTER, 0, -16);

    g_vals = lv_label_create(parent);
    lv_obj_set_style_text_font(g_vals, UI_FONT_M, 0);
    lv_obj_set_style_text_color(g_vals, lv_color_hex(COL_TXT2), 0);
    lv_obj_set_style_text_align(g_vals, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(g_vals, "");
    lv_obj_align(g_vals, LV_ALIGN_CENTER, 0, 40);

    // 进 app 临时关 WiFi 让单天线全给 BLE(退出恢复)
    s_wifi_prev = wifi_service_enabled();
    if (s_wifi_prev) wifi_service_set_enabled(false);

    s_has_rx = false; s_shown_link = -1; s_linked = false; s_started = false;
    ble_twin_set_rx_cb(on_rx);
    if (ble_twin_start()) {
        portENTER_CRITICAL(&s_mux); s_run = true; portEXIT_CRITICAL(&s_mux);
        if (!s_task && xTaskCreate(sampler, "twin", 4096, NULL, 5, &s_task) != pdPASS) {
            portENTER_CRITICAL(&s_mux); s_run = false; portEXIT_CRITICAL(&s_mux);
            lv_label_set_text(g_status, "sampler start fail");
        } else { s_started = true; }
    } else {
        lv_label_set_text(g_status, "BLE init fail\n(enable NimBLE in sdkconfig)");
    }
}

static void twin_tick(void) {
    if (!g_status || !s_started) return;
    portENTER_CRITICAL(&s_mux);
    bool linked = s_linked;
    int soc = s_soc;
    float ax = s_ax, ay = s_ay, az = s_az, gz = s_gz;
    portEXIT_CRITICAL(&s_mux);
    if (s_shown_link != (int)linked) {
        ui_text(g_status, linked ? "linked" : "advertising as GeekTwin\nopen web/twin.html");
        ui_bg_color(g_dot, linked ? COL_CHARGE : COL_TXT2); s_shown_link = linked;
    }
    char b[128];
    snprintf(b, sizeof b, "batt %d%%\nax %+.2f ay %+.2f az %+.2f\ngz %+d dps%s",
             soc, ax, ay, az, (int)gz, s_has_rx ? "\nrx ok" : "");
    ui_text(g_vals, b);
}

static void twin_exit(void) {
    portENTER_CRITICAL(&s_mux); s_run = false; portEXIT_CRITICAL(&s_mux);

    ble_twin_set_rx_cb(NULL);                         // 退出后不再接收网页指令,避免 BLE 回调写已退出 app 的状态
    ble_twin_stop();
    if (s_wifi_prev) wifi_service_set_enabled(true);   // 恢复 WiFi
    g_status = g_vals = g_dot = NULL;
    s_has_rx = false;
}

const app_t app_twin = { "twin", COL_TXT, twin_enter, twin_tick, twin_exit, NULL, 100 };
