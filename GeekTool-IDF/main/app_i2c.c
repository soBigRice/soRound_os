// I2C 扫描器 —— 扫出板载芯片地址,带已知芯片名。用 IDF i2c_master_probe。
#include "app.h"
#include "ui_list.h"
#include "board_config.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "i2c";

static const char *i2c_name(uint8_t a) {
    switch (a) {
        case 0x18: return "ES8311 codec";
        case 0x20: return "TCA9554 IO";
        case 0x34: return "AXP2101 PMU";
        case 0x40: return "ES7210 ADC";
        case 0x51: return "PCF85063 RTC";
        case 0x5a: return "CST9217 touch";
        case 0x6a:
        case 0x6b: return "QMI8658 IMU";
        default:   return "";
    }
}

static lv_obj_t *g_list;
static TaskHandle_t s_worker;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_generation, s_done_generation;
static bool s_requested, s_active;
static bool s_found[0x78];
static bool s_shown;

static void scan_worker(void *arg) {
    (void)arg;
    for (;;) {
        portENTER_CRITICAL(&s_mux);
        bool requested = s_requested;
        uint32_t generation = s_generation;
        s_requested = false;
        if (!requested) s_worker = NULL;
        portEXIT_CRITICAL(&s_mux);
        if (!requested) { vTaskDelete(NULL); return; }
        bool found[0x78] = {0};
        for (uint8_t a = 0x08; a < 0x78; a++) {
            portENTER_CRITICAL(&s_mux);
            bool current = s_active && s_generation == generation;
            portEXIT_CRITICAL(&s_mux);
            if (!current) break;
            found[a] = i2c_master_probe(board_i2c_bus(), a, 20) == ESP_OK;
            // 把共享 I2C 总线留给触摸/传感器,不连续占满整个地址范围。
            vTaskDelay(1);
        }
        portENTER_CRITICAL(&s_mux);
        if (s_active && s_generation == generation) {
            memcpy(s_found, found, sizeof found); s_done_generation = generation;
        }
        portEXIT_CRITICAL(&s_mux);
    }
}
static void i2c_enter(lv_obj_t *parent) {
    g_list = ui_list_create(parent); s_shown = false;
    ui_list_row(g_list, "Scanning I2C...", NULL, 0); ui_list_relayout(g_list);
    portENTER_CRITICAL(&s_mux);
    s_active = s_requested = true; s_generation++;
    portEXIT_CRITICAL(&s_mux);
    if (!s_worker && xTaskCreate(scan_worker, "i2c_scan", 3072, NULL, 3, &s_worker) != pdPASS) {
        lv_obj_clean(g_list); ui_list_row(g_list, "Scan task failed", NULL, 0);
        ui_list_relayout(g_list); s_shown = true; return;
    }
}
static void i2c_tick(void) {
    if (!g_list || s_shown) return;
    bool found[0x78];
    portENTER_CRITICAL(&s_mux);
    bool done = s_done_generation == s_generation;
    if (done) memcpy(found, s_found, sizeof found);
    portEXIT_CRITICAL(&s_mux);
    if (!done) return;
    s_shown = true; lv_obj_clean(g_list);
    int count = 0;
    for (uint8_t a = 0x08; a < 0x78; a++) if (found[a]) {
        char addr[8]; snprintf(addr, sizeof addr, "0x%02X", a);
        const char *nm = i2c_name(a);
        ui_list_row(g_list, addr, nm[0] ? nm : "?", nm[0] ? COL_OK : COL_TXT2); count++;
    }
    if (!count) ui_list_row(g_list, "No I2C device", NULL, 0);
    ui_list_relayout(g_list); ESP_LOGI(TAG, "found %d device(s)", count);
}
static void i2c_exit(void) {
    portENTER_CRITICAL(&s_mux); s_active = false; s_requested = false; s_generation++; portEXIT_CRITICAL(&s_mux);
    g_list = NULL;
}
const app_t app_i2c = { "I2C", COL_I2C, i2c_enter, i2c_tick, i2c_exit };
