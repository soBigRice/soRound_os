// WiFi 扫描 + 连接 —— 第一个 app(复用 ui_list)
#include "app.h"
#include "ui_list.h"
#include <WiFi.h>
#include <string.h>

static lv_obj_t *g_list = NULL;
static lv_obj_t *g_status = NULL;
static bool scanning = false, connecting = false;
static uint32_t connect_start = 0;
static char sel_ssid[33] = {0};
static lv_obj_t *dlg = NULL, *pwd_ta = NULL, *kb = NULL;

/* ---------- 连接 ---------- */
static void set_status(const char *txt, uint32_t color) {
  if (!g_status) return;
  lv_label_set_text(g_status, txt);
  lv_obj_set_style_text_color(g_status, lv_color_hex(color), 0);
}

static void start_connect(const char *ssid, const char *pass) {
  strncpy(sel_ssid, ssid, sizeof(sel_ssid) - 1);
  sel_ssid[sizeof(sel_ssid) - 1] = 0;
  Serial.printf("[WiFi] connecting to \"%s\"\n", ssid);
  if (pass && strlen(pass) > 0) WiFi.begin(ssid, pass);
  else                          WiFi.begin(ssid);
  connecting = true;
  connect_start = millis();
  set_status("Connecting...", COL_TXT2);
}

static void close_dialog(void) {
  /* 延迟删除:close 常由对话框内按钮的事件触发,直接删会崩 */
  if (dlg) { lv_obj_del_async(dlg); dlg = NULL; pwd_ta = NULL; kb = NULL; }
}

static void connect_btn_event(lv_event_t *e) {
  if (!pwd_ta) return;
  char pass[65]; strncpy(pass, lv_textarea_get_text(pwd_ta), sizeof(pass) - 1); pass[64] = 0;
  char ssid[33]; strncpy(ssid, sel_ssid, sizeof(ssid));
  close_dialog();
  start_connect(ssid, pass);
}
static void cancel_btn_event(lv_event_t *e) { close_dialog(); }
static void kb_event(lv_event_t *e) {
  lv_event_code_t c = lv_event_get_code(e);
  if (c == LV_EVENT_READY)       connect_btn_event(e);
  else if (c == LV_EVENT_CANCEL) close_dialog();
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

  lv_obj_t *t = lv_label_create(dlg);
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  lv_obj_set_width(t, 300);
  lv_label_set_text_fmt(t, "%s", ssid);
  lv_obj_set_style_text_color(t, lv_color_hex(COL_TXT2), 0);
  lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 46);

  pwd_ta = lv_textarea_create(dlg);
  lv_textarea_set_one_line(pwd_ta, true);
  lv_textarea_set_password_mode(pwd_ta, false);
  lv_textarea_set_placeholder_text(pwd_ta, "password");
  lv_obj_set_width(pwd_ta, 360);
  lv_obj_set_style_text_font(pwd_ta, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(pwd_ta, lv_color_white(), 0);
  lv_obj_set_style_bg_color(pwd_ta, lv_color_hex(0x1c1c22), 0);
  lv_obj_set_style_border_color(pwd_ta, lv_color_hex(COL_WIFI), 0);
  lv_obj_set_style_border_width(pwd_ta, 2, 0);
  lv_obj_set_style_radius(pwd_ta, 10, 0);
  lv_obj_align(pwd_ta, LV_ALIGN_TOP_MID, 0, 78);

  lv_obj_t *ok = lv_btn_create(dlg);
  lv_obj_set_size(ok, 150, 48);
  lv_obj_set_style_bg_color(ok, lv_color_hex(COL_OK), 0);
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

  kb = lv_keyboard_create(dlg);
  lv_obj_set_size(kb, lv_pct(96), 196);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(kb, pwd_ta);
  lv_obj_add_event_cb(kb, kb_event, LV_EVENT_ALL, NULL);
}

/* ---------- 列表 ---------- */
static void wifi_row_click(lv_event_t *e) {
  lv_obj_t *row = lv_event_get_target(e);
  bool secured  = (bool)(intptr_t)lv_obj_get_user_data(row);
  const char *ssid = lv_label_get_text(lv_obj_get_child(row, 0));
  if (secured) show_password_dialog(ssid);
  else         start_connect(ssid, "");
}

static uint32_t rssi_color(int rssi) {
  if (rssi > -60) return COL_OK;
  if (rssi > -75) return COL_I2C;   // amber
  return COL_WARN;
}

static void wifi_populate(int found) {
  lv_obj_clean(g_list);
  int rows = 0;
  for (int i = 0; i < found; i++) {
    String s = WiFi.SSID(i);
    if (s.length() == 0) s = "<hidden>";
    int rssi = WiFi.RSSI(i);
    char rs[8]; snprintf(rs, sizeof(rs), "%d", rssi);
    bool secured = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    lv_obj_t *row = ui_list_row(g_list, s.c_str(), rs, rssi_color(rssi));
    lv_obj_set_user_data(row, (void *)(intptr_t)secured);
    lv_obj_add_event_cb(row, wifi_row_click, LV_EVENT_CLICKED, NULL);
    rows++;
  }
  static const char *demo[] = {"Xiaomi_AX6000", "MERCURY_F8B2", "ChinaNet-Home",
                               "Office_2.4G", "Guest_Network", "Lab-AP-07"};
  int di = 0;
  while (rows < 10) {
    lv_obj_t *row = ui_list_row(g_list, demo[di % 6], "--", COL_TXT2);
    lv_obj_set_user_data(row, (void *)(intptr_t) true);
    lv_obj_add_event_cb(row, wifi_row_click, LV_EVENT_CLICKED, NULL);
    di++; rows++;
  }
  ui_list_relayout(g_list);
}

/* ---------- App 生命周期 ---------- */
static void wifi_enter(lv_obj_t *parent) {
  g_list = ui_list_create(parent);

  g_status = lv_label_create(parent);
  lv_label_set_text(g_status, "");
  lv_obj_set_style_text_color(g_status, lv_color_hex(COL_TXT2), 0);
  lv_obj_set_style_bg_color(g_status, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_status, LV_OPA_70, 0);
  lv_obj_set_style_pad_hor(g_status, 10, 0);
  lv_obj_set_style_pad_ver(g_status, 4, 0);
  lv_obj_set_style_radius(g_status, 10, 0);
  lv_obj_align(g_status, LV_ALIGN_BOTTOM_MID, 0, -28);

  wifi_populate(0);

  WiFi.mode(WIFI_STA);
  WiFi.scanNetworks(true);   // 异步
  scanning = true;
  set_status("Scanning...", COL_TXT2);
}

static void wifi_tick(void) {
  if (scanning) {
    int r = WiFi.scanComplete();
    if (r >= 0) {
      scanning = false;
      wifi_populate(r);
      WiFi.scanDelete();
      set_status("", COL_TXT2);
      Serial.printf("[WiFi] scan done: %d\n", r);
    }
  }
  if (connecting) {
    wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
      connecting = false;
      Serial.print("[WiFi] IP: "); Serial.println(WiFi.localIP());
      char b[40]; snprintf(b, sizeof(b), "OK: %s", sel_ssid);
      set_status(b, COL_OK);
    } else if (millis() - connect_start > 15000) {
      connecting = false;
      set_status("Connect failed", COL_WARN);
    }
  }
}

static void wifi_exit(void) {
  close_dialog();
  if (scanning) { WiFi.scanDelete(); scanning = false; }
  connecting = false;
  g_list = NULL;
  g_status = NULL;
}

const app_t app_wifi = { "WiFi", COL_WIFI, wifi_enter, wifi_tick, wifi_exit };
