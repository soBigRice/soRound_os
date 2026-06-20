// Nothing 风格点阵表盘。时间用 5×7 小圆点画(手绘点阵),外环 60 点,红点冒号 + 秒点。
// 低运动:数字每分钟重建一次,冒号 1Hz 闪,秒点每秒挪一下 —— 天然符合避撕裂原则。
#include "watchface.h"
#include "app.h"
#include "power.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include <stdio.h>
#include <time.h>
#include <math.h>

#define WF_CX   233
#define WF_CY   233
#define RING_R  213
#define P       13     // 点距
#define R       5      // 数字点半径
#define Y0      150    // 数字顶行 y(上移给下方信息区腾地方)
#define NRED    0xd1283a   // Nothing 红

// 5×7 点阵字模(0-9)
static const char *const DIGITS[10][7] = {
    {"01110","10001","10011","10101","11001","10001","01110"},
    {"00100","01100","00100","00100","00100","00100","01110"},
    {"01110","10001","00001","00010","00100","01000","11111"},
    {"11110","00001","00001","01110","00001","00001","11110"},
    {"00010","00110","01010","10010","11111","00010","00010"},
    {"11111","10000","11110","00001","00001","10001","01110"},
    {"00110","01000","10000","11110","10001","10001","01110"},
    {"11111","00001","00010","00100","01000","01000","01000"},
    {"01110","10001","10001","01110","10001","10001","01110"},
    {"01110","10001","10001","01111","00001","00010","01100"},
};
static const int CELL_X[4] = { 76, 151, 250, 325 };   // 4 个数字格的左 x(HH:MM)

static lv_obj_t *wf_screen, *wf_time, *wf_colon[2], *wf_sec, *wf_date, *wf_wifi, *wf_bat;
static lv_timer_t *wf_timer;
static int  s_last_min = -1;
static bool s_colon_on = true;
static int  s_bat_div  = 0;

static lv_obj_t *mkdot(lv_obj_t *par, int cx, int cy, int r, uint32_t color, lv_opa_t opa) {
    lv_obj_t *d = lv_obj_create(par);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, r * 2, r * 2);
    lv_obj_set_pos(d, cx - r, cy - r);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(d, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(d, opa, 0);
    lv_obj_remove_flag(d, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(d, LV_OBJ_FLAG_EVENT_BUBBLE);   // 让手势冒泡到 wf_screen(上滑解锁)
    return d;
}

static void draw_digit(char ch, int ox) {
    if (ch < '0' || ch > '9') return;
    const char *const *g = DIGITS[ch - '0'];
    for (int r = 0; r < 7; r++)
        for (int c = 0; c < 5; c++)
            if (g[r][c] == '1')
                mkdot(wf_time, ox + c * P + P / 2, Y0 + r * P + P / 2, R, COL_TXT, LV_OPA_COVER);
}

static void update_date(struct tm *tm) {
    static const char *const wd[7]  = { "sun","mon","tue","wed","thu","fri","sat" };
    static const char *const mo[12] = { "jan","feb","mar","apr","may","jun","jul","aug","sep","oct","nov","dec" };
    char d[24];
    snprintf(d, sizeof d, "%s  %02d  %s", wd[tm->tm_wday], tm->tm_mday, mo[tm->tm_mon]);
    lv_label_set_text(wf_date, d);
}

static void update_net_and_battery(void) {
    // WiFi 名(没连/没初始化都显示 wifi off)
    wifi_ap_record_t ap;
    bool wifi_ok = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK);
    lv_label_set_text(wf_wifi, wifi_ok ? (const char *)ap.ssid : "wifi off");
    lv_obj_set_style_text_color(wf_wifi, lv_color_hex(wifi_ok ? COL_WIFI : COL_TXT2), 0);

    // IP(连上才有)
    char ip[24] = "";
    if (wifi_ok) {
        esp_netif_t *nif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t ipi;
        if (nif && esp_netif_get_ip_info(nif, &ipi) == ESP_OK && ipi.ip.addr)
            snprintf(ip, sizeof ip, "  " IPSTR, IP2STR(&ipi.ip));
    }

    // 电量 + IP(+充电色/⚡)
    int soc; pwr_state_t st;
    if (power_read(&soc, &st)) {
        bool charging = (st == PWR_CHARGING || st == PWR_FULL);
        char b[48];
        snprintf(b, sizeof b, "%s bat %d%%%s", charging ? LV_SYMBOL_CHARGE : "", soc, ip);
        lv_label_set_text(wf_bat, b);
        lv_obj_set_style_text_color(wf_bat, lv_color_hex(charging ? COL_WIFI : COL_TXT2), 0);
    }
}

static void wf_tick(lv_timer_t *t) {
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);

    if (tm.tm_min != s_last_min) {            // 整分才重建数字
        lv_obj_clean(wf_time);
        char hh[3], mm[3];
        snprintf(hh, sizeof hh, "%02d", tm.tm_hour);
        snprintf(mm, sizeof mm, "%02d", tm.tm_min);
        draw_digit(hh[0], CELL_X[0]);
        draw_digit(hh[1], CELL_X[1]);
        draw_digit(mm[0], CELL_X[2]);
        draw_digit(mm[1], CELL_X[3]);
        update_date(&tm);
        update_net_and_battery();
        s_last_min = tm.tm_min;
    }

    s_colon_on = !s_colon_on;                 // 冒号 1Hz 闪
    lv_opa_t opa = s_colon_on ? LV_OPA_COVER : LV_OPA_TRANSP;
    lv_obj_set_style_bg_opa(wf_colon[0], opa, 0);
    lv_obj_set_style_bg_opa(wf_colon[1], opa, 0);

    float a = tm.tm_sec / 60.0f * 6.2832f - 1.5708f;   // 秒点沿环
    lv_obj_set_pos(wf_sec, WF_CX + (int)(cosf(a) * RING_R) - 4,
                           WF_CY + (int)(sinf(a) * RING_R) - 4);

    if (++s_bat_div >= 4) { s_bat_div = 0; update_net_and_battery(); }   // 每 ~2s 刷网络/电量
}

void watchface_init(void) {
    wf_screen = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(wf_screen);
    lv_obj_set_size(wf_screen, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(wf_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(wf_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(wf_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(wf_screen, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < 60; i++) {            // 点阵外环
        float a = i / 60.0f * 6.2832f - 1.5708f;
        bool big = (i % 5 == 0);
        mkdot(wf_screen, WF_CX + (int)(cosf(a) * RING_R), WF_CY + (int)(sinf(a) * RING_R),
              big ? 3 : 2, COL_TXT, big ? LV_OPA_60 : LV_OPA_30);
    }

    wf_time = lv_obj_create(wf_screen);       // 数字容器(每分钟 clean+重建)
    lv_obj_remove_style_all(wf_time);
    lv_obj_set_size(wf_time, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(wf_time, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(wf_time, LV_OBJ_FLAG_EVENT_BUBBLE);

    wf_colon[0] = mkdot(wf_screen, WF_CX, Y0 + 2 * P + P / 2, R, NRED, LV_OPA_COVER);
    wf_colon[1] = mkdot(wf_screen, WF_CX, Y0 + 4 * P + P / 2, R, NRED, LV_OPA_COVER);
    wf_sec      = mkdot(wf_screen, WF_CX, WF_CY - RING_R, 4, COL_TXT, LV_OPA_COVER);
    mkdot(wf_screen, WF_CX, 40, 3, NRED, LV_OPA_COVER);   // 顶端"已锁定"红点

    wf_date = lv_label_create(wf_screen);
    lv_obj_set_style_text_font(wf_date, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(wf_date, lv_color_hex(COL_TXT2), 0);
    lv_label_set_text(wf_date, "");
    lv_obj_align(wf_date, LV_ALIGN_TOP_MID, 0, 262);

    wf_wifi = lv_label_create(wf_screen);
    lv_obj_set_style_text_font(wf_wifi, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(wf_wifi, lv_color_hex(COL_TXT2), 0);
    lv_label_set_text(wf_wifi, "");
    lv_obj_align(wf_wifi, LV_ALIGN_TOP_MID, 0, 286);

    wf_bat = lv_label_create(wf_screen);
    lv_obj_set_style_text_font(wf_bat, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(wf_bat, lv_color_hex(COL_TXT2), 0);
    lv_label_set_text(wf_bat, "");
    lv_obj_align(wf_bat, LV_ALIGN_TOP_MID, 0, 310);
}

void watchface_show(void) {
    if (!wf_screen) return;
    s_last_min = -1;                          // 强制重建数字
    lv_obj_remove_flag(wf_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(wf_screen);
    if (!wf_timer) wf_timer = lv_timer_create(wf_tick, 500, NULL);
    wf_tick(NULL);                            // 立即刷一帧
}

void watchface_hide(void) {
    if (!wf_screen) return;
    lv_obj_add_flag(wf_screen, LV_OBJ_FLAG_HIDDEN);
    if (wf_timer) { lv_timer_delete(wf_timer); wf_timer = NULL; }
}

bool watchface_visible(void) {
    return wf_screen && !lv_obj_has_flag(wf_screen, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t *watchface_root(void) { return wf_screen; }
