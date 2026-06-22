// WiFi 扫描 + 连接 —— 复用 ui_list。从 Arduino WiFi 移植到 IDF esp_wifi(LVGL 9)。
//
// 线程约定(关键):esp_wifi/IP 事件在事件任务里回调,只允许写 volatile 标志位,
// 绝不在事件回调里碰 LVGL。所有 UI 更新都放在 wifi_tick()(由 launcher 的 lv_timer
// 在 LVGL 任务里调度)——和 Arduino 版的"轮询"模型一致,从而无需给 LVGL 上额外锁。
#include "app.h"
#include "ui_list.h"
#include "board_config.h"
#include <string.h>
#include <stdio.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "rtc.h"
#include <sys/time.h>

static const char *TAG = "wifi";

#define SCAN_MAX     20
#define CONNECT_TMO  15000   // 连接超时(ms)

static lv_obj_t *g_list = NULL, *g_status = NULL;
static lv_obj_t *dlg = NULL, *pwd_ta = NULL, *kb = NULL;
static char sel_ssid[33] = {0};

// 事件标志:事件任务写,wifi_tick 读
static volatile bool s_inited       = false;
static volatile bool s_scanning     = false;
static volatile bool s_scan_done    = false;
static volatile bool s_connecting   = false;
static volatile bool s_got_ip       = false;
static volatile bool s_disconnected = false;
static volatile bool s_suppress_rc  = false;   // 切换 AP 的瞬间抑制自动重连(避免重连到旧 AP)
static uint32_t      connect_start  = 0;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* ---------- esp_wifi / IP 事件(只设标志,勿碰 LVGL) ---------- */
static void wifi_evt(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            // 开机/重启:若 NVS 里有记住的 AP(FLASH 存储会自动加载),直接自动连
            wifi_config_t wc;
            if (esp_wifi_get_config(WIFI_IF_STA, &wc) == ESP_OK && wc.sta.ssid[0]) esp_wifi_connect();
        } else if (id == WIFI_EVENT_SCAN_DONE) {
            s_scan_done = true;
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            s_got_ip = false;
            s_disconnected = true;
            if (!s_suppress_rc) esp_wifi_connect();   // 掉线自动重连(回到信号范围/AP 恢复即重连)
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_got_ip = true;
    }
}

static void on_time_sync(struct timeval *tv) { (void)tv; rtc_save_from_system(); }   // SNTP 校时回调

/* 一次性初始化协议栈(nvs 已在 main 里 init)。多次进入 app 只初始化一次。 */
static void wifi_svc_init(void) {
    if (s_inited) return;
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_evt, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_evt, NULL, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));   // ★凭据写 NVS,重启/重烧后还在
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());                            // → STA_START 事件里自动连记住的 AP

    // SNTP 校时:连上后自动同步(时区在 main 里设为 CST-8),供表盘显示真实时间
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(on_time_sync);   // 校时成功 → 写回 RTC,断电也准
    esp_sntp_init();

    s_inited = true;
}

/* ---------- 状态条(LVGL 任务) ---------- */
static void set_status(const char *txt, uint32_t color) {
    if (!g_status) return;
    lv_label_set_text(g_status, txt);
    lv_obj_set_style_text_color(g_status, lv_color_hex(color), 0);
}

static void start_scan(void) {
    esp_wifi_scan_stop();              // 取消可能残留的扫描(无则忽略)
    s_scan_done = false;
    wifi_scan_config_t sc = { .show_hidden = true };
    if (esp_wifi_scan_start(&sc, false) != ESP_OK) {   // 异步
        s_scanning = false;
        set_status("Scan failed", COL_WARN);
        return;
    }
    s_scanning = true;
    set_status("Scanning...", COL_TXT2);
}

static void start_connect(const char *ssid, const char *pass) {
    strncpy(sel_ssid, ssid, sizeof(sel_ssid) - 1);
    sel_ssid[sizeof(sel_ssid) - 1] = 0;

    wifi_config_t wc = { 0 };
    strncpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid) - 1);
    if (pass && pass[0]) strncpy((char *)wc.sta.password, pass, sizeof(wc.sta.password) - 1);

    s_suppress_rc = true;              // 抑制下面 disconnect 触发的"自动重连回旧 AP"
    esp_wifi_disconnect();             // 若已连别的 AP,先断开(未连接则忽略错误)
    esp_wifi_set_config(WIFI_IF_STA, &wc);   // FLASH 存储 → 自动写入 NVS,记住这个 AP
    s_got_ip = false;
    s_disconnected = false;
    s_suppress_rc = false;
    esp_wifi_connect();
    s_connecting = true;
    connect_start = now_ms();
    ESP_LOGI(TAG, "connecting to \"%s\"", ssid);
    set_status("Connecting...", COL_TXT2);
}

/* ---------- 密码对话框(LVGL 任务) ---------- */
static void close_dialog(void) {
    // 延迟删除:close 常由对话框内按钮事件触发,直接删会崩
    if (dlg) { lv_obj_delete_async(dlg); dlg = NULL; pwd_ta = NULL; kb = NULL; }
}

static void connect_btn_event(lv_event_t *e) {
    if (!pwd_ta) return;
    char pass[65]; strncpy(pass, lv_textarea_get_text(pwd_ta), sizeof(pass) - 1); pass[64] = 0;
    char ssid[33]; strncpy(ssid, sel_ssid, sizeof(ssid) - 1); ssid[32] = 0;
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
    lv_obj_set_size(dlg, LCD_H_RES, LCD_V_RES);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(dlg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dlg, 0, 0);
    lv_obj_set_style_pad_all(dlg, 0, 0);
    lv_obj_remove_flag(dlg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(dlg);
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(t, 300);
    lv_label_set_text(t, ssid);
    lv_obj_set_style_text_color(t, lv_color_hex(COL_TXT2), 0);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 46);

    pwd_ta = lv_textarea_create(dlg);
    lv_textarea_set_one_line(pwd_ta, true);
    lv_textarea_set_password_mode(pwd_ta, false);
    lv_textarea_set_placeholder_text(pwd_ta, "password");
    lv_obj_set_width(pwd_ta, 360);
    lv_obj_set_style_text_font(pwd_ta, UI_FONT_L, 0);
    lv_obj_set_style_text_color(pwd_ta, lv_color_white(), 0);
    lv_obj_set_style_bg_color(pwd_ta, lv_color_hex(0x1c1c22), 0);
    lv_obj_set_style_border_color(pwd_ta, lv_color_hex(COL_WIFI), 0);
    lv_obj_set_style_border_width(pwd_ta, 2, 0);
    lv_obj_set_style_radius(pwd_ta, 10, 0);
    lv_obj_align(pwd_ta, LV_ALIGN_TOP_MID, 0, 78);

    lv_obj_t *ok = lv_button_create(dlg);
    lv_obj_set_size(ok, 150, 48);
    lv_obj_set_style_bg_color(ok, lv_color_hex(COL_OK), 0);
    lv_obj_align(ok, LV_ALIGN_TOP_MID, -82, 138);
    lv_obj_add_event_cb(ok, connect_btn_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *okl = lv_label_create(ok);
    lv_label_set_text(okl, "Connect");
    lv_obj_set_style_text_color(okl, lv_color_black(), 0);
    lv_obj_center(okl);

    lv_obj_t *cancel = lv_button_create(dlg);
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

/* ---------- 列表(LVGL 任务) ---------- */
static uint32_t rssi_color(int rssi) {
    if (rssi > -60) return COL_OK;
    if (rssi > -75) return COL_I2C;   // amber
    return COL_WARN;
}

static void wifi_row_click(lv_event_t *e) {
    lv_obj_t *row = lv_event_get_target_obj(e);
    bool secured  = (bool)(intptr_t)lv_obj_get_user_data(row);
    const char *ssid = lv_label_get_text(lv_obj_get_child(row, 0));
    if (secured) show_password_dialog(ssid);
    else         start_connect(ssid, "");
}

static void wifi_populate(void) {
    if (!g_list) return;
    lv_obj_clean(g_list);

    static wifi_ap_record_t recs[SCAN_MAX];
    uint16_t n = SCAN_MAX;
    if (esp_wifi_scan_get_ap_records(&n, recs) != ESP_OK) n = 0;

    for (uint16_t i = 0; i < n; i++) {
        const char *ssid = recs[i].ssid[0] ? (const char *)recs[i].ssid : "<hidden>";
        char rs[8]; snprintf(rs, sizeof(rs), "%d", recs[i].rssi);
        bool secured = (recs[i].authmode != WIFI_AUTH_OPEN);
        lv_obj_t *row = ui_list_row(g_list, ssid, rs, rssi_color(recs[i].rssi));
        lv_obj_set_user_data(row, (void *)(intptr_t)secured);
        lv_obj_add_event_cb(row, wifi_row_click, LV_EVENT_CLICKED, NULL);
    }
    if (n == 0) ui_list_row(g_list, "No networks", NULL, 0);
    ui_list_relayout(g_list);
    ESP_LOGI(TAG, "scan done: %u AP(s)", (unsigned)n);
}

/* ---------- App 生命周期 ---------- */
static void wifi_enter(lv_obj_t *parent) {
    wifi_svc_init();

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

    s_connecting = false;
    start_scan();
}

static void wifi_tick(void) {
    if (s_scanning && s_scan_done) {
        s_scanning = false;
        wifi_populate();
        set_status("", COL_TXT2);
    }
    if (s_connecting) {
        if (s_got_ip) {
            s_connecting = false;
            char b[48]; snprintf(b, sizeof(b), "OK: %s", sel_ssid);
            set_status(b, COL_OK);
        } else if (s_disconnected && now_ms() - connect_start > 2000) {
            // 2s 宽限:跳过切换 AP 时旧连接断开的瞬时事件,之后的断开按"认证失败"处理
            s_connecting = false;
            set_status("Connect failed", COL_WARN);
        } else if (now_ms() - connect_start > CONNECT_TMO) {
            s_connecting = false;
            set_status("Connect timeout", COL_WARN);
        }
    }
}

static void wifi_exit(void) {
    close_dialog();
    s_scanning = false;
    s_connecting = false;
    g_list = NULL;
    g_status = NULL;
    // 保留已连接状态,不 deinit wifi(下次进入更快)
}

// 开机自动起 WiFi 协议栈(STA_START 事件里会自动连记住的 AP)。由 main 在启动末尾调用,幂等。
void wifi_service_start(void) { wifi_svc_init(); }

const app_t app_wifi = { "WiFi", COL_WIFI, wifi_enter, wifi_tick, wifi_exit };
