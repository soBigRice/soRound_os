/*
 * WiFi 列表压测 + 连接 — ESP32-S3-Touch-AMOLED-1.75C
 * ------------------------------------------------------------
 * 圆屏可滚动 WiFi 列表(居中聚焦 + 边缘虚化 + 实时 FPS),点击可连接。
 *
 * 底层初始化(CO5300 显示 / CST9217 触摸 / LVGL 移植)沿用微雪官方
 * 05_LVGL_Widgets 例程的已验证写法。
 *
 * 曲率效果:用 translate_x + 透明度(LVGL 官方 lv_example_scroll_6 的做法),
 *           不用 transform_zoom —— 后者会导致只显示一行 + 帧率暴跌。
 *
 * 依赖库:lvgl 8.4.0 / GFX Library for Arduino 1.6.4 / SensorLib 0.3.3
 * 板卡/配置见 README.md(注意 esp32 core 用 3.3.5)
 */
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <math.h>
#include <string.h>
#include <lvgl.h>
#include "Arduino_GFX_Library.h"
#include "TouchDrvCSTXXX.hpp"
#include "pin_config.h"
#include "esp_timer.h"

/* ---------- 显示(CO5300 / QSPI) ---------- */
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *gfx = new Arduino_CO5300(
    bus, LCD_RESET, 0 /*rotation*/, LCD_WIDTH, LCD_HEIGHT, 6, 0, 0, 0);

/* ---------- 触摸(CST9217 / I2C) ---------- */
TouchDrvCST92xx touch;
int16_t tp_x[5], tp_y[5];

/* ---------- LVGL ---------- */
#define TICK_MS 2
static lv_disp_draw_buf_t draw_buf;
static uint32_t screenW, screenH;

static lv_obj_t *wifi_list   = NULL;
static lv_obj_t *fps_label   = NULL;
static lv_obj_t *title_label = NULL;

#define FADE_R  210   // 透明度衰减范围(像素)
#define CURVE_R 300   // 水平曲率半径(越大越平缓)
#define ROW_W   384
#define ROW_H   58

/* FPS 统计(实际渲染帧) */
static volatile uint32_t frame_cnt = 0;
static uint32_t fps_last_ms = 0;

/* WiFi 状态 */
static bool scanning = false;
static bool connecting = false;
static uint32_t connect_start = 0;
static char sel_ssid[33] = {0};

/* 密码键盘对话框 */
static lv_obj_t *dlg = NULL, *pwd_ta = NULL, *kb = NULL;

/* ============ LVGL 显示回调 ============ */
static void disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *px) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
#if (LV_COLOR_16_SWAP != 0)
  gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)px, w, h);
#else
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px, w, h);
#endif
  lv_disp_flush_ready(disp);
}

/* CO5300 要求刷新窗口偶数对齐 */
static void disp_rounder(lv_disp_drv_t *d, lv_area_t *a) {
  if (a->x1 % 2) a->x1--;
  if (a->y1 % 2) a->y1--;
  if (a->x2 % 2 == 0) a->x2++;
  if (a->y2 % 2 == 0) a->y2++;
}

static void disp_monitor(lv_disp_drv_t *d, uint32_t t, uint32_t px) { frame_cnt++; }

/* ============ 触摸读取(轮询) ============ */
static void touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  uint8_t n = touch.getPoint(tp_x, tp_y, 1);
  if (n > 0) {
    data->state   = LV_INDEV_STATE_PR;
    data->point.x = tp_x[0];
    data->point.y = tp_y[0];
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

static void lv_tick_cb(void *arg) { lv_tick_inc(TICK_MS); }

/* ============ 圆屏曲率:translate_x + 透明度(便宜又稳) ============ */
static void curve_scroll_cb(lv_event_t *e) {
  lv_obj_t *cont = lv_event_get_target(e);
  lv_area_t a; lv_obj_get_coords(cont, &a);
  lv_coord_t cy = a.y1 + lv_area_get_height(&a) / 2;

  uint32_t cnt = lv_obj_get_child_cnt(cont);
  for (uint32_t i = 0; i < cnt; i++) {
    lv_obj_t *row = lv_obj_get_child(cont, i);
    lv_area_t r; lv_obj_get_coords(row, &r);
    lv_coord_t ry   = r.y1 + lv_area_get_height(&r) / 2;
    lv_coord_t diff = LV_ABS(ry - cy);

    /* 水平内移,跟随圆弧 */
    lv_coord_t dc = diff > CURVE_R ? CURVE_R : diff;
    lv_coord_t x  = CURVE_R - (lv_coord_t)sqrtf((float)(CURVE_R * CURVE_R - dc * dc));
    lv_obj_set_style_translate_x(row, x, 0);

    /* 离中心越远越淡 */
    lv_coord_t df  = diff > FADE_R ? FADE_R : diff;
    lv_opa_t   opa = lv_map(df, 0, FADE_R, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_obj_set_style_opa(row, opa, 0);

    /* 居中聚焦:淡淡高亮胶囊 */
    bool focus = diff < ROW_H * 0.6;
    lv_obj_set_style_bg_opa(row, focus ? LV_OPA_10 : LV_OPA_TRANSP, 0);
  }
}

/* ============ 点击某行 → 连接 ============ */
static void close_dialog() {
  if (dlg) { lv_obj_del(dlg); dlg = NULL; pwd_ta = NULL; kb = NULL; }
}

static void start_connect(const char *ssid, const char *pass) {
  strncpy(sel_ssid, ssid, sizeof(sel_ssid) - 1);
  sel_ssid[sizeof(sel_ssid) - 1] = 0;
  Serial.printf("Connecting to \"%s\" ...\n", ssid);
  if (pass && strlen(pass) > 0) WiFi.begin(ssid, pass);
  else                          WiFi.begin(ssid);
  connecting = true;
  connect_start = millis();
  if (title_label) lv_label_set_text_fmt(title_label, "Connecting...");
}

/* 读密码框 → 连接(替代被圆形裁掉的键盘 ✓) */
static void connect_btn_event(lv_event_t *e) {
  if (!pwd_ta) return;
  char pass[65]; strncpy(pass, lv_textarea_get_text(pwd_ta), sizeof(pass) - 1); pass[64] = 0;
  char ssid[33]; strncpy(ssid, sel_ssid, sizeof(ssid));
  close_dialog();
  start_connect(ssid, pass);
}

static void cancel_btn_event(lv_event_t *e) { close_dialog(); }

/* 若键盘自带的 ✓/✗ 恰好能点到,也照样生效 */
static void kb_event(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY)       connect_btn_event(e);
  else if (code == LV_EVENT_CANCEL) close_dialog();
}

static void show_password_dialog(const char *ssid) {
  strncpy(sel_ssid, ssid, sizeof(sel_ssid) - 1);
  sel_ssid[sizeof(sel_ssid) - 1] = 0;

  dlg = lv_obj_create(lv_layer_top());
  lv_obj_set_size(dlg, screenW, screenH);
  lv_obj_center(dlg);
  lv_obj_set_style_bg_color(dlg, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(dlg, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(dlg, 0, 0);
  lv_obj_set_style_pad_all(dlg, 0, 0);
  lv_obj_clear_flag(dlg, LV_OBJ_FLAG_SCROLLABLE);

  /* 网络名(放中间宽处,清晰) */
  lv_obj_t *t = lv_label_create(dlg);
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  lv_obj_set_width(t, 300);
  lv_label_set_text_fmt(t, "%s", ssid);
  lv_obj_set_style_text_color(t, lv_color_hex(0xcfd3d7), 0);
  lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 46);

  /* 密码框:放大 + 明文显示,看得清 */
  pwd_ta = lv_textarea_create(dlg);
  lv_textarea_set_one_line(pwd_ta, true);
  lv_textarea_set_password_mode(pwd_ta, false);          // 明文,便于看清
  lv_textarea_set_placeholder_text(pwd_ta, "password");
  lv_obj_set_width(pwd_ta, 360);
  lv_obj_set_style_text_font(pwd_ta, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(pwd_ta, lv_color_white(), 0);
  lv_obj_set_style_bg_color(pwd_ta, lv_color_hex(0x1c1c22), 0);
  lv_obj_set_style_border_color(pwd_ta, lv_color_hex(0x4aa3ff), 0);
  lv_obj_set_style_border_width(pwd_ta, 2, 0);
  lv_obj_set_style_radius(pwd_ta, 10, 0);
  lv_obj_align(pwd_ta, LV_ALIGN_TOP_MID, 0, 78);

  /* 大号 Connect / Cancel 按钮(放在圆最宽的中部,不会被裁) */
  lv_obj_t *ok = lv_btn_create(dlg);
  lv_obj_set_size(ok, 150, 48);
  lv_obj_set_style_bg_color(ok, lv_color_hex(0x3ddc84), 0);
  lv_obj_align(ok, LV_ALIGN_TOP_MID, -82, 138);
  lv_obj_add_event_cb(ok, connect_btn_event, LV_EVENT_CLICKED, NULL);
  lv_obj_t *okl = lv_label_create(ok);
  lv_label_set_text(okl, "Connect");
  lv_obj_set_style_text_color(okl, lv_color_black(), 0);
  lv_obj_center(okl);

  lv_obj_t *cancel = lv_btn_create(dlg);
  lv_obj_set_size(cancel, 150, 48);
  lv_obj_set_style_bg_color(cancel, lv_color_hex(0x33343a), 0);
  lv_obj_align(cancel, LV_ALIGN_TOP_MID, 82, 138);
  lv_obj_add_event_cb(cancel, cancel_btn_event, LV_EVENT_CLICKED, NULL);
  lv_obj_t *cl = lv_label_create(cancel);
  lv_label_set_text(cl, "Cancel");
  lv_obj_set_style_text_color(cl, lv_color_white(), 0);
  lv_obj_center(cl);

  /* 键盘(底部;宽度收窄减少裁切。打字用上面几排,提交用 Connect 按钮) */
  kb = lv_keyboard_create(dlg);
  lv_obj_set_size(kb, lv_pct(96), 196);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(kb, pwd_ta);
  lv_obj_add_event_cb(kb, kb_event, LV_EVENT_ALL, NULL);
}

static void row_click(lv_event_t *e) {
  lv_obj_t *row = lv_event_get_target(e);
  bool secured  = (bool)(intptr_t)lv_obj_get_user_data(row);
  lv_obj_t *name = lv_obj_get_child(row, 0);     // 第一个子对象 = SSID label
  const char *ssid = lv_label_get_text(name);
  if (secured) show_password_dialog(ssid);
  else         start_connect(ssid, "");          // 开放网络直接连
}

/* ============ 一行:SSID + 信号格 ============ */
static void add_wifi_row(const char *ssid, int level, bool secured) {
  lv_obj_t *row = lv_obj_create(wifi_list);
  lv_obj_set_size(row, ROW_W, ROW_H);
  lv_obj_set_style_bg_color(row, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_radius(row, ROW_H / 2, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_user_data(row, (void *)(intptr_t)secured);
  lv_obj_add_event_cb(row, row_click, LV_EVENT_CLICKED, NULL);

  lv_obj_t *name = lv_label_create(row);
  lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
  lv_obj_set_width(name, ROW_W - 96);
  lv_label_set_text(name, ssid);
  lv_obj_set_style_text_color(name, lv_color_white(), 0);
  lv_obj_set_style_text_font(name, &lv_font_montserrat_20, 0);
  lv_obj_align(name, LV_ALIGN_LEFT_MID, 24, 0);
  lv_obj_clear_flag(name, LV_OBJ_FLAG_CLICKABLE);   // 让点击落到 row 上

  lv_obj_t *bars = lv_obj_create(row);
  lv_obj_set_size(bars, 26, 22);
  lv_obj_set_style_bg_opa(bars, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(bars, 0, 0);
  lv_obj_set_style_pad_all(bars, 0, 0);
  lv_obj_clear_flag(bars, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(bars, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_align(bars, LV_ALIGN_RIGHT_MID, -22, 0);
  for (int b = 0; b < 3; b++) {
    lv_obj_t *bar = lv_obj_create(bars);
    lv_obj_set_size(bar, 5, 8 + b * 6);
    lv_obj_set_style_radius(bar, 1, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(bar, (b < level) ? lv_color_hex(0x4aa3ff) : lv_color_hex(0x34343c), 0);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, b * 8, 0);
  }
}

static int rssi_level(int rssi) {
  if (rssi > -55) return 3;
  if (rssi > -67) return 2;
  if (rssi > -78) return 1;
  return 0;
}

/* 填充列表:扫描结果 + 不足 14 行用 Demo 补足 */
static void populate_list(int found) {
  lv_obj_clean(wifi_list);
  int rows = 0;
  for (int i = 0; i < found; i++) {
    String s = WiFi.SSID(i);
    if (s.length() == 0) s = "<hidden>";
    bool secured = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    add_wifi_row(s.c_str(), rssi_level(WiFi.RSSI(i)), secured);
    rows++;
  }
  static const char *demo[] = {
      "Xiaomi_AX6000", "MERCURY_F8B2", "ChinaNet-Home", "Starbucks_Free",
      "TP-LINK_5G_88", "DIRECT-HP-M283", "Office_2.4G", "Guest_Network",
      "Lab-AP-07", "Roof_5G"};
  int di = 0;
  while (rows < 14) { add_wifi_row(demo[di % 10], (di % 3) + 1, true); di++; rows++; }

  if (title_label) lv_label_set_text_fmt(title_label, "Wi-Fi  -  %d", found);
  lv_obj_update_layout(wifi_list);                   // 先生效布局,行坐标才有效
  lv_event_send(wifi_list, LV_EVENT_SCROLL, NULL);   // 立即应用曲率
}

/* ============ 顶层悬浮:电量环 + 标题 + FPS ============ */
static void build_overlay() {
  lv_obj_t *top = lv_layer_top();

  lv_obj_t *batt = lv_arc_create(top);
  lv_obj_set_size(batt, 458, 458);
  lv_obj_center(batt);
  lv_arc_set_rotation(batt, 270);
  lv_arc_set_bg_angles(batt, 0, 360);
  lv_arc_set_range(batt, 0, 100);
  lv_arc_set_value(batt, 72);
  lv_obj_remove_style(batt, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(batt, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_color(batt, lv_color_hex(0x15151a), LV_PART_MAIN);
  lv_obj_set_style_arc_width(batt, 8, LV_PART_MAIN);
  lv_obj_set_style_arc_color(batt, lv_color_hex(0x3ddc84), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(batt, 8, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(batt, true, LV_PART_INDICATOR);

  title_label = lv_label_create(top);
  lv_label_set_text(title_label, "Wi-Fi");
  lv_obj_set_style_text_color(title_label, lv_color_hex(0xcfd3d7), 0);
  lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_bg_color(title_label, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(title_label, LV_OPA_70, 0);
  lv_obj_set_style_pad_hor(title_label, 10, 0);
  lv_obj_set_style_pad_ver(title_label, 4, 0);
  lv_obj_set_style_radius(title_label, 10, 0);
  lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 28);

  fps_label = lv_label_create(top);
  lv_label_set_text(fps_label, "-- FPS");
  lv_obj_set_style_text_color(fps_label, lv_color_hex(0x4aa3ff), 0);
  lv_obj_set_style_text_font(fps_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_bg_color(fps_label, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(fps_label, LV_OPA_70, 0);
  lv_obj_set_style_pad_hor(fps_label, 10, 0);
  lv_obj_set_style_pad_ver(fps_label, 4, 0);
  lv_obj_set_style_radius(fps_label, 10, 0);
  lv_obj_align(fps_label, LV_ALIGN_BOTTOM_MID, 0, -32);
}

static void build_ui() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  wifi_list = lv_obj_create(scr);
  lv_obj_set_size(wifi_list, screenW, screenH);
  lv_obj_center(wifi_list);
  lv_obj_set_style_bg_opa(wifi_list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(wifi_list, 0, 0);
  lv_obj_set_flex_flow(wifi_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(wifi_list, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(wifi_list, 6, 0);
  lv_obj_set_style_pad_top(wifi_list, screenH / 2 - ROW_H / 2, 0);
  lv_obj_set_style_pad_bottom(wifi_list, screenH / 2 - ROW_H / 2, 0);
  lv_obj_set_scroll_dir(wifi_list, LV_DIR_VER);
  lv_obj_set_scroll_snap_y(wifi_list, LV_SCROLL_SNAP_CENTER);
  lv_obj_set_scrollbar_mode(wifi_list, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_event_cb(wifi_list, curve_scroll_cb, LV_EVENT_SCROLL, NULL);

  build_overlay();
  populate_list(0);
}

void setup() {
  Serial.begin(115200);

  /* I2C + 触摸 */
  Wire.begin(IIC_SDA, IIC_SCL);
  pinMode(TP_RST, OUTPUT);
  digitalWrite(TP_RST, LOW);  delay(30);
  digitalWrite(TP_RST, HIGH); delay(50);
  delay(200);
  touch.setPins(TP_RST, TP_INT);
  if (!touch.begin(Wire, 0x5A, IIC_SDA, IIC_SCL)) {
    Serial.println("CST9217 touch not found!");
  } else {
    Serial.print("Touch model: ");
    Serial.println(touch.getModelName());
  }
  touch.setMaxCoordinates(466, 466);
  touch.setMirrorXY(true, true);

  /* 显示 */
  gfx->begin();
  gfx->setBrightness(200);
  screenW = gfx->width();
  screenH = gfx->height();

  /* LVGL */
  lv_init();
  uint32_t bufPx = screenW * screenH / 10;
  lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(bufPx * sizeof(lv_color_t), MALLOC_CAP_DMA);
  lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(bufPx * sizeof(lv_color_t), MALLOC_CAP_DMA);
  if (!buf1) buf1 = (lv_color_t *)heap_caps_malloc(bufPx * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  if (!buf2) buf2 = (lv_color_t *)heap_caps_malloc(bufPx * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, bufPx);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res    = screenW;
  disp_drv.ver_res    = screenH;
  disp_drv.flush_cb   = disp_flush;
  disp_drv.rounder_cb = disp_rounder;
  disp_drv.monitor_cb = disp_monitor;
  disp_drv.draw_buf   = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type    = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touch_read;
  lv_indev_drv_register(&indev_drv);

  const esp_timer_create_args_t tick_args = { .callback = &lv_tick_cb, .name = "lv_tick" };
  esp_timer_handle_t tick_timer;
  esp_timer_create(&tick_args, &tick_timer);
  esp_timer_start_periodic(tick_timer, TICK_MS * 1000);

  build_ui();

  /* WiFi 异步扫描 */
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  WiFi.scanNetworks(true);
  scanning = true;

  fps_last_ms = millis();
  Serial.println("Setup done");
}

void loop() {
  lv_timer_handler();

  /* 扫描完成 → 填充 */
  if (scanning) {
    int r = WiFi.scanComplete();
    if (r >= 0) {
      scanning = false;
      populate_list(r);
      WiFi.scanDelete();
      Serial.printf("Scan done: %d APs\n", r);
    }
  }

  /* 连接状态 */
  if (connecting) {
    wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
      connecting = false;
      Serial.print("Connected! IP: "); Serial.println(WiFi.localIP());
      if (title_label) lv_label_set_text_fmt(title_label, "OK: %s", sel_ssid);
      lv_obj_set_style_text_color(title_label, lv_color_hex(0x3ddc84), 0);
    } else if (millis() - connect_start > 15000) {
      connecting = false;
      Serial.println("Connect timeout/failed");
      if (title_label) lv_label_set_text(title_label, "Connect failed");
      lv_obj_set_style_text_color(title_label, lv_color_hex(0xff6b6b), 0);
    }
  }

  /* 每秒刷新 FPS(实际渲染帧:静止≈0,滚动时才是真实帧率) */
  uint32_t now = millis();
  if (now - fps_last_ms >= 1000) {
    if (fps_label) lv_label_set_text_fmt(fps_label, "%u FPS", (unsigned)frame_cnt);
    frame_cnt = 0;
    fps_last_ms = now;
  }

  delay(2);
}
