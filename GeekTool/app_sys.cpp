// 系统信息 —— 芯片 / 内存 / 运行时长(运行时长与空闲内存实时刷新)
#include "app.h"
#include "ui_list.h"

static lv_obj_t *g_heap_row = NULL;
static lv_obj_t *g_up_row   = NULL;
static uint32_t  g_last_ms  = 0;

static void fmt_uptime(char *buf, size_t n, uint32_t ms) {
  uint32_t s = ms / 1000, h = s / 3600, m = (s % 3600) / 60, sec = s % 60;
  snprintf(buf, n, "%lu:%02lu:%02lu", (unsigned long)h, (unsigned long)m, (unsigned long)sec);
}

static void sys_enter(lv_obj_t *parent) {
  lv_obj_t *list = ui_list_create(parent);
  char buf[32];

  ui_list_row(list, "Chip", ESP.getChipModel(), COL_TXT2);
  snprintf(buf, sizeof(buf), "%d", ESP.getChipCores());
  ui_list_row(list, "Cores", buf, COL_TXT2);
  snprintf(buf, sizeof(buf), "%lu MHz", (unsigned long)ESP.getCpuFreqMHz());
  ui_list_row(list, "CPU", buf, COL_TXT2);
  snprintf(buf, sizeof(buf), "%lu MB", (unsigned long)(ESP.getFlashChipSize() / 1048576));
  ui_list_row(list, "Flash", buf, COL_TXT2);
  snprintf(buf, sizeof(buf), "%lu MB", (unsigned long)(ESP.getPsramSize() / 1048576));
  ui_list_row(list, "PSRAM", buf, COL_TXT2);
  snprintf(buf, sizeof(buf), "%lu KB", (unsigned long)(ESP.getFreeHeap() / 1024));
  g_heap_row = ui_list_row(list, "Free heap", buf, COL_OK);
  ui_list_row(list, "SDK", ESP.getSdkVersion(), COL_TXT2);
  fmt_uptime(buf, sizeof(buf), millis());
  g_up_row = ui_list_row(list, "Uptime", buf, COL_OK);

  ui_list_relayout(list);
}

static void sys_tick(void) {
  if (millis() - g_last_ms < 500) return;
  g_last_ms = millis();
  char buf[32];
  if (g_heap_row) {
    lv_obj_t *r = ui_list_row_right(g_heap_row);
    if (r) { snprintf(buf, sizeof(buf), "%lu KB", (unsigned long)(ESP.getFreeHeap() / 1024)); lv_label_set_text(r, buf); }
  }
  if (g_up_row) {
    lv_obj_t *r = ui_list_row_right(g_up_row);
    if (r) { fmt_uptime(buf, sizeof(buf), millis()); lv_label_set_text(r, buf); }
  }
}

static void sys_exit(void) { g_heap_row = NULL; g_up_row = NULL; }

const app_t app_sys = { "System", COL_SYS, sys_enter, sys_tick, sys_exit };
