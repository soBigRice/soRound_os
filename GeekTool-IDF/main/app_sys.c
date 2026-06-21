// 系统信息 —— 芯片 / 内存 / 运行时长(运行时长与空闲内存实时刷新)。IDF 原生 API。
#include "app.h"
#include "ui_list.h"
#include <stdio.h>
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_idf_version.h"
#include "esp_heap_caps.h"

static lv_obj_t *g_heap_row = NULL;
static lv_obj_t *g_int_row  = NULL;
static lv_obj_t *g_up_row   = NULL;
static uint32_t  g_last_ms  = 0;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void fmt_uptime(char *buf, size_t n, uint32_t ms) {
    uint32_t s = ms / 1000, h = s / 3600, m = (s % 3600) / 60, sec = s % 60;
    snprintf(buf, n, "%lu:%02lu:%02lu", (unsigned long)h, (unsigned long)m, (unsigned long)sec);
}

static void sys_enter(lv_obj_t *parent) {
    lv_obj_t *list = ui_list_create(parent);
    char buf[40];

    esp_chip_info_t info;
    esp_chip_info(&info);

    ui_list_row(list, "Chip", CONFIG_IDF_TARGET, COL_TXT2);
    snprintf(buf, sizeof(buf), "%d", info.cores);
    ui_list_row(list, "Cores", buf, COL_TXT2);
    snprintf(buf, sizeof(buf), "%d MHz", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    ui_list_row(list, "CPU", buf, COL_TXT2);

    uint32_t flash_sz = 0;
    esp_flash_get_size(NULL, &flash_sz);
    snprintf(buf, sizeof(buf), "%lu MB", (unsigned long)(flash_sz / (1024 * 1024)));
    ui_list_row(list, "Flash", buf, COL_TXT2);

    snprintf(buf, sizeof(buf), "%u MB", (unsigned)(esp_psram_get_size() / (1024 * 1024)));
    ui_list_row(list, "PSRAM", buf, COL_TXT2);

    snprintf(buf, sizeof(buf), "%lu KB", (unsigned long)(esp_get_free_heap_size() / 1024));
    g_heap_row = ui_list_row(list, "Free heap", buf, COL_OK);

    snprintf(buf, sizeof(buf), "%lu KB", (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    g_int_row = ui_list_row(list, "Int free", buf, COL_OK);   // 内部 RAM(DMA 用);开关 app 后若持续下降=泄漏

    ui_list_row(list, "SDK", esp_get_idf_version(), COL_TXT2);

    fmt_uptime(buf, sizeof(buf), now_ms());
    g_up_row = ui_list_row(list, "Uptime", buf, COL_OK);

    ui_list_relayout(list);
}

static void sys_tick(void) {
    if (now_ms() - g_last_ms < 500) return;
    g_last_ms = now_ms();
    char buf[32];
    if (g_heap_row) {
        lv_obj_t *r = ui_list_row_right(g_heap_row);
        if (r) { snprintf(buf, sizeof(buf), "%lu KB", (unsigned long)(esp_get_free_heap_size() / 1024)); lv_label_set_text(r, buf); }
    }
    if (g_int_row) {
        lv_obj_t *r = ui_list_row_right(g_int_row);
        if (r) { snprintf(buf, sizeof(buf), "%lu KB", (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024)); lv_label_set_text(r, buf); }
    }
    if (g_up_row) {
        lv_obj_t *r = ui_list_row_right(g_up_row);
        if (r) { fmt_uptime(buf, sizeof(buf), now_ms()); lv_label_set_text(r, buf); }
    }
}

static void sys_exit(void) { g_heap_row = NULL; g_int_row = NULL; g_up_row = NULL; }

const app_t app_sys = { "System", COL_SYS, sys_enter, sys_tick, sys_exit };
