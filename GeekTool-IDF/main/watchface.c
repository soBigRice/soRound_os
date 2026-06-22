// 锁屏表盘框架(多表盘 + 低功耗 AOD)+ 4 款表盘(dots/bold/rings/image)。
// 全屏黑底(AMOLED 省电),Nothing 单色 + 唯一红强调。手绘 5×7 点阵数字。
// 低运动:数字每分钟重建;活动态冒号 0.5Hz 闪 + 秒点沿环;AOD 态冒号常亮、秒点隐藏、只按分钟刷新。
#include "watchface.h"
#include "app.h"
#include "power.h"
#include "settings.h"
#include "img_store.h"
#include "glyph.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

#define WF_CX   233
#define WF_CY   233
#define RING_R  213

// 表盘接口:每款表盘实现 build/update/destroy,注册进 FACES[]
typedef struct {
    const char *name;                 // ASCII,切换时短暂显示
    void (*build)(lv_obj_t *root);    // 在 root(wf_content)上建全部 UI
    void (*update)(const struct tm *t, bool aod, bool min_changed);  // 周期刷新(框架按状态调度)
    void (*destroy)(void);            // 清理表盘私有指针(内容已随 wf_content 销毁)
} watchface_t;

// 5×7 点阵字模(0-9)—— 各表盘共用
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

/* ===== 共用基元 ===== */
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
    lv_obj_add_flag(d, LV_OBJ_FLAG_EVENT_BUBBLE);   // 手势冒泡到 wf_screen(上滑解锁 / 左右滑换表盘)
    return d;
}

// 在 (ox,oy) 左上角画一个 5×7 点阵数字
static void draw_digit_at(lv_obj_t *par, char ch, int ox, int oy, int pitch, int r, uint32_t color, lv_opa_t opa) {
    if (ch < '0' || ch > '9') return;
    const char *const *g = DIGITS[ch - '0'];
    for (int row = 0; row < 7; row++)
        for (int c = 0; c < 5; c++)
            if (g[row][c] == '1')
                mkdot(par, ox + c * pitch + pitch / 2, oy + row * pitch + pitch / 2, r, color, opa);
}

// 居中画 HH:MM 的 4 个数字(不含冒号),返回冒号中心 x;冒号由调用方画(便于闪烁/着色)
static int draw_time_digits(lv_obj_t *par, const struct tm *t, int cx, int cy, int pitch, int r, uint32_t col, lv_opa_t opa) {
    char b[5];
    snprintf(b, sizeof b, "%02d%02d", t->tm_hour, t->tm_min);
    int dw = 5 * pitch, g = pitch, colw = pitch;
    int total = 4 * dw + 4 * g + colw;
    int x0 = cx - total / 2, oy = cy - (7 * pitch) / 2, ox = x0;
    draw_digit_at(par, b[0], ox, oy, pitch, r, col, opa); ox += dw + g;
    draw_digit_at(par, b[1], ox, oy, pitch, r, col, opa); ox += dw + g;
    int colon_cx = ox + colw / 2; ox += colw + g;
    draw_digit_at(par, b[2], ox, oy, pitch, r, col, opa); ox += dw + g;
    draw_digit_at(par, b[3], ox, oy, pitch, r, col, opa);
    return colon_cx;
}

static lv_obj_t *mklabel(lv_obj_t *par, const lv_font_t *font, uint32_t color, int y) {
    lv_obj_t *l = lv_label_create(par);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, "");
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_add_flag(l, LV_OBJ_FLAG_EVENT_BUBBLE);
    return l;
}

/* ============================================================ Dots 表盘 ============================================================ */
#define D_P   13
#define D_R   5
#define D_Y0  150
static lv_obj_t *d_time, *d_colon[2], *d_sec, *d_date, *d_wifi, *d_bat;
static bool      d_colon_on;

static void dots_date(const struct tm *t) {
    static const char *const wd[7]  = { "sun","mon","tue","wed","thu","fri","sat" };
    static const char *const mo[12] = { "jan","feb","mar","apr","may","jun","jul","aug","sep","oct","nov","dec" };
    char s[24];
    snprintf(s, sizeof s, "%s  %02d  %s", wd[t->tm_wday], t->tm_mday, mo[t->tm_mon]);
    lv_label_set_text(d_date, s);
}

static void dots_netbat(void) {
    wifi_ap_record_t ap;
    bool wifi_ok = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK);
    lv_label_set_text(d_wifi, wifi_ok ? (const char *)ap.ssid : "wifi off");
    lv_obj_set_style_text_color(d_wifi, lv_color_hex(wifi_ok ? COL_WIFI : COL_TXT2), 0);

    char ip[24] = "";
    if (wifi_ok) {
        esp_netif_t *nif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t ipi;
        if (nif && esp_netif_get_ip_info(nif, &ipi) == ESP_OK && ipi.ip.addr)
            snprintf(ip, sizeof ip, "  " IPSTR, IP2STR(&ipi.ip));
    }
    int soc; pwr_state_t st;
    if (power_read(&soc, &st)) {
        bool charging = (st == PWR_CHARGING || st == PWR_FULL);
        char b[48];
        snprintf(b, sizeof b, "%s bat %d%%%s", charging ? LV_SYMBOL_CHARGE : "", soc, ip);
        lv_label_set_text(d_bat, b);
        lv_obj_set_style_text_color(d_bat, lv_color_hex(charging ? COL_WIFI : COL_TXT2), 0);
    }
}

static void dots_build(lv_obj_t *root) {
    for (int i = 0; i < 60; i++) {            // 点阵外环(分钟刻度)
        float a = i / 60.0f * 6.2832f - 1.5708f;
        bool big = (i % 5 == 0);
        mkdot(root, WF_CX + (int)(cosf(a) * RING_R), WF_CY + (int)(sinf(a) * RING_R),
              big ? 3 : 2, COL_TXT, big ? LV_OPA_60 : LV_OPA_30);
    }
    d_time = lv_obj_create(root);
    lv_obj_remove_style_all(d_time);
    lv_obj_set_size(d_time, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(d_time, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(d_time, LV_OBJ_FLAG_EVENT_BUBBLE);

    int oy = D_Y0;
    d_colon[0] = mkdot(root, WF_CX, oy + 2 * D_P + D_P / 2, D_R, COL_RED, LV_OPA_COVER);
    d_colon[1] = mkdot(root, WF_CX, oy + 4 * D_P + D_P / 2, D_R, COL_RED, LV_OPA_COVER);
    d_sec      = mkdot(root, WF_CX, WF_CY - RING_R, 4, COL_TXT, LV_OPA_COVER);

    d_date = mklabel(root, UI_FONT_M, COL_TXT2, 262);
    d_wifi = mklabel(root, UI_FONT_M, COL_TXT2, 286);
    d_bat  = mklabel(root, &lv_font_montserrat_14, COL_TXT2, 310);
    d_colon_on = true;
}

static void dots_update(const struct tm *t, bool aod, bool mc) {
    if (mc) {
        lv_obj_clean(d_time);
        draw_time_digits(d_time, t, WF_CX, D_Y0 + (7 * D_P) / 2, D_P, D_R, COL_TXT, LV_OPA_COVER);
        dots_date(t);
        dots_netbat();
    }
    lv_opa_t copa;
    if (aod) copa = LV_OPA_COVER;
    else { d_colon_on = !d_colon_on; copa = d_colon_on ? LV_OPA_COVER : LV_OPA_TRANSP; }
    lv_obj_set_style_bg_opa(d_colon[0], copa, 0);
    lv_obj_set_style_bg_opa(d_colon[1], copa, 0);

    if (aod) {
        lv_obj_add_flag(d_sec, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(d_sec, LV_OBJ_FLAG_HIDDEN);
        float a = t->tm_sec / 60.0f * 6.2832f - 1.5708f;
        lv_obj_set_pos(d_sec, WF_CX + (int)(cosf(a) * RING_R) - 4, WF_CY + (int)(sinf(a) * RING_R) - 4);
    }
}

static void dots_destroy(void) { d_time = d_colon[0] = d_colon[1] = d_sec = d_date = d_wifi = d_bat = NULL; }

/* ============================================================ Bold 表盘(极简大字,HH 上 / MM 下) ============================================================ */
#define B_P   22
#define B_R   8
#define B_X0  112
#define B_X1  244
#define B_YH  64
#define B_YM  248
static lv_obj_t *b_time, *b_dot, *b_date;
static bool      b_on;

static void bold_build(lv_obj_t *root) {
    b_time = lv_obj_create(root);
    lv_obj_remove_style_all(b_time);
    lv_obj_set_size(b_time, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(b_time, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b_time, LV_OBJ_FLAG_EVENT_BUBBLE);

    b_dot = mkdot(root, WF_CX, WF_CY, 7, COL_RED, LV_OPA_COVER);   // 两行之间居中的红心跳点
    b_date = mklabel(root, UI_FONT_M, COL_TXT2, 0);
    lv_obj_align(b_date, LV_ALIGN_BOTTOM_MID, 0, -36);
    b_on = true;
}

static void bold_update(const struct tm *t, bool aod, bool mc) {
    if (mc) {
        lv_obj_clean(b_time);
        char hh[3], mm[3];
        snprintf(hh, sizeof hh, "%02d", t->tm_hour);
        snprintf(mm, sizeof mm, "%02d", t->tm_min);
        draw_digit_at(b_time, hh[0], B_X0, B_YH, B_P, B_R, COL_TXT, LV_OPA_COVER);
        draw_digit_at(b_time, hh[1], B_X1, B_YH, B_P, B_R, COL_TXT, LV_OPA_COVER);
        draw_digit_at(b_time, mm[0], B_X0, B_YM, B_P, B_R, COL_TXT, LV_OPA_COVER);
        draw_digit_at(b_time, mm[1], B_X1, B_YM, B_P, B_R, COL_TXT, LV_OPA_COVER);
        static const char *const wd[7]  = { "sun","mon","tue","wed","thu","fri","sat" };
        static const char *const mo[12] = { "jan","feb","mar","apr","may","jun","jul","aug","sep","oct","nov","dec" };
        char s[24]; snprintf(s, sizeof s, "%s  %02d %s", wd[t->tm_wday], t->tm_mday, mo[t->tm_mon]);
        lv_label_set_text(b_date, s);
    }
    lv_opa_t opa;
    if (aod) opa = LV_OPA_COVER;
    else { b_on = !b_on; opa = b_on ? LV_OPA_COVER : LV_OPA_30; }
    lv_obj_set_style_bg_opa(b_dot, opa, 0);
}

static void bold_destroy(void) { b_time = b_dot = b_date = NULL; }

/* ============================================================ Rings 表盘(同心点环:外=分钟,内=小时,中心数字) ============================================================ */
#define RG_RO 205
#define RG_RI 150
static lv_obj_t *r_ring, *r_center;

static void rings_build(lv_obj_t *root) {
    r_ring = lv_obj_create(root);
    lv_obj_remove_style_all(r_ring);
    lv_obj_set_size(r_ring, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(r_ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(r_ring, LV_OBJ_FLAG_EVENT_BUBBLE);
    r_center = lv_obj_create(root);
    lv_obj_remove_style_all(r_center);
    lv_obj_set_size(r_center, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(r_center, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(r_center, LV_OBJ_FLAG_EVENT_BUBBLE);
}

static void rings_update(const struct tm *t, bool aod, bool mc) {
    (void)aod;
    if (!mc) return;                          // 同心环天然平静:仅按分钟重建
    lv_obj_clean(r_ring);
    int mn = t->tm_min, hr = t->tm_hour % 12;
    for (int i = 0; i < 60; i++) {            // 外环:分钟进度
        float a = i / 60.0f * 6.2832f - 1.5708f;
        int x = WF_CX + (int)(cosf(a) * RG_RO), y = WF_CY + (int)(sinf(a) * RG_RO);
        if (i < mn)       mkdot(r_ring, x, y, 3, COL_TXT, LV_OPA_COVER);
        else if (i == mn) mkdot(r_ring, x, y, 4, COL_RED, LV_OPA_COVER);   // 当前分钟:红色引导点
        else              mkdot(r_ring, x, y, 2, COL_TXT, LV_OPA_30);
    }
    for (int h = 0; h < 12; h++) {            // 内环:小时进度
        float a = h / 12.0f * 6.2832f - 1.5708f;
        int x = WF_CX + (int)(cosf(a) * RG_RI), y = WF_CY + (int)(sinf(a) * RG_RI);
        if (h < hr)       mkdot(r_ring, x, y, 4, COL_TXT, LV_OPA_COVER);
        else if (h == hr) mkdot(r_ring, x, y, 5, COL_TXT, LV_OPA_COVER);
        else              mkdot(r_ring, x, y, 3, COL_TXT, LV_OPA_30);
    }
    lv_obj_clean(r_center);                    // 中心小号数字 HH:MM
    int cc = draw_time_digits(r_center, t, WF_CX, WF_CY, 9, 3, COL_TXT, LV_OPA_COVER);
    int oy = WF_CY - (7 * 9) / 2;
    mkdot(r_center, cc, oy + 2 * 9 + 4, 3, COL_RED, LV_OPA_COVER);
    mkdot(r_center, cc, oy + 4 * 9 + 4, 3, COL_RED, LV_OPA_COVER);
}

static void rings_destroy(void) { r_ring = r_center = NULL; }

/* ============================================================ Image 表盘(全屏 JPEG 背景 + 时间叠加) ============================================================ */
#define IM_CY 392
static lv_obj_t *im_time, *im_msg;

static void image_build(lv_obj_t *root) {
    const lv_image_dsc_t *dsc = img_store_face_image();
    if (dsc) {
        lv_obj_t *img = lv_image_create(root);
        lv_image_set_src(img, dsc);
        lv_obj_center(img);
        lv_obj_add_flag(img, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_t *scrim = lv_obj_create(root);   // 时间区半透明深色衬底,保证可读
        lv_obj_remove_style_all(scrim);
        lv_obj_set_size(scrim, 320, 96);
        lv_obj_align(scrim, LV_ALIGN_BOTTOM_MID, 0, -28);
        lv_obj_set_style_radius(scrim, 16, 0);
        lv_obj_set_style_bg_color(scrim, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(scrim, LV_OPA_60, 0);
        lv_obj_remove_flag(scrim, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(scrim, LV_OBJ_FLAG_EVENT_BUBBLE);
    } else {
        im_msg = lv_label_create(root);
        lv_obj_set_style_text_font(im_msg, UI_FONT_M, 0);
        lv_obj_set_style_text_color(im_msg, lv_color_hex(COL_TXT2), 0);
        lv_obj_set_style_text_align(im_msg, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(im_msg, "no image\nput a 466x466\nbg.jpg in images/");
        lv_obj_center(im_msg);
        lv_obj_add_flag(im_msg, LV_OBJ_FLAG_EVENT_BUBBLE);
    }
    im_time = lv_obj_create(root);
    lv_obj_remove_style_all(im_time);
    lv_obj_set_size(im_time, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(im_time, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(im_time, LV_OBJ_FLAG_EVENT_BUBBLE);
}

static void image_update(const struct tm *t, bool aod, bool mc) {
    (void)aod;
    if (!mc) return;                          // 图片表盘平静:冒号常亮、无秒点,仅按分钟刷新
    lv_obj_clean(im_time);
    int cc = draw_time_digits(im_time, t, WF_CX, IM_CY, 10, 4, COL_TXT, LV_OPA_COVER);
    int oy = IM_CY - (7 * 10) / 2;
    mkdot(im_time, cc, oy + 2 * 10 + 5, 4, COL_RED, LV_OPA_COVER);
    mkdot(im_time, cc, oy + 4 * 10 + 5, 4, COL_RED, LV_OPA_COVER);
}

static void image_destroy(void) { im_time = im_msg = NULL; }

/* ============================================================ Weather 表盘(时间 + 实时天气,数据来自 app_weather) ============================================================ */
static lv_obj_t *wx_time, *wx_iconc, *wx_tempc, *wx_info, *wx_msg;
static int       wx_shown;

static void wf_weather_icon(lv_obj_t *p, int code, int cx, int cy) {
    if (code <= 1) {                                       // 晴
        glyph_circle(p, cx, cy, 15, 8, 3, COL_TXT);
        glyph_dot(p, cx, cy, 5, COL_RED);
        for (int k = 0; k < 8; k++) { float a = k * 0.7854f; glyph_dot(p, cx + (int)(cosf(a) * 26), cy + (int)(sinf(a) * 26), 3, COL_TXT); }
    } else if ((code >= 71 && code <= 77) || code == 85 || code == 86) {   // 雪
        glyph_circle(p, cx, cy - 6, 20, 10, 3, COL_TXT);
        for (int i = -1; i <= 1; i++) glyph_dot(p, cx + i * 16, cy + 22, 4, COL_TXT);
    } else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82) || code >= 95) {   // 雨/雷
        glyph_circle(p, cx, cy - 6, 20, 10, 3, COL_TXT);
        for (int i = -1; i <= 1; i++) glyph_line(p, cx + i * 16 + 4, cy + 14, cx + i * 16 - 4, cy + 32, 9, 3, COL_RED);
    } else {                                               // 多云/雾
        glyph_circle(p, cx, cy, 22, 11, 3, COL_TXT);
        glyph_dot(p, cx, cy, 4, COL_TXT2);
    }
}

static void wf_draw_temp(lv_obj_t *par, int t, int cy, int pitch, int r) {
    char s[8]; int v = t < 0 ? -t : t;
    snprintf(s, sizeof s, "%d", v);
    int n = (int)strlen(s), dw = 5 * pitch, gap = pitch;
    int total = (t < 0 ? dw + gap : 0) + n * dw + (n - 1) * gap + 2 * pitch;
    int oy = cy - (7 * pitch) / 2, ox = WF_CX - total / 2;
    if (t < 0) { for (int c = 1; c <= 3; c++) mkdot(par, ox + c * pitch + pitch / 2, oy + 3 * pitch + pitch / 2, r, COL_TXT, LV_OPA_COVER); ox += dw + gap; }
    for (int k = 0; k < n; k++) { draw_digit_at(par, s[k], ox, oy, pitch, r, COL_TXT, LV_OPA_COVER); ox += dw + gap; }
    glyph_circle(par, ox + pitch, oy + pitch, pitch / 2 + 1, 6, 2, COL_TXT);   // 度环 °
}

static lv_obj_t *wx_full(lv_obj_t *root) {
    lv_obj_t *c = lv_obj_create(root);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_EVENT_BUBBLE);
    return c;
}

static void weather_build(lv_obj_t *root) {
    wx_time  = wx_full(root);
    wx_iconc = wx_full(root);
    wx_tempc = wx_full(root);
    wx_info = mklabel(root, UI_FONT_M, COL_TXT2, 312);
    wx_msg  = mklabel(root, UI_FONT_M, COL_TXT2, 250);
    wx_shown = -99999;
}

static void weather_update(const struct tm *t, bool aod, bool mc) {
    (void)aod;
    if (mc) {                                              // 顶部 HH:MM(小号),每分钟重画
        lv_obj_clean(wx_time);
        int oy = 96;
        int cc = draw_time_digits(wx_time, t, WF_CX, oy + (7 * 9) / 2, 9, 3, COL_TXT, LV_OPA_COVER);
        mkdot(wx_time, cc, oy + 2 * 9 + 4, 3, COL_RED, LV_OPA_COVER);
        mkdot(wx_time, cc, oy + 4 * 9 + 4, 3, COL_RED, LV_OPA_COVER);
    }
    weather_poll();                                        // 后台按需拉(连着 WiFi 才拉)
    int temp, lo, hi, code, hum;
    bool ok = weather_cached(&temp, &lo, &hi, &code, &hum);
    int key = ok ? (code * 1000 + temp + 500) : -1;
    if (key != wx_shown) {                                 // 天气变了才重画图标/温度
        wx_shown = key;
        lv_obj_clean(wx_iconc);
        lv_obj_clean(wx_tempc);
        if (ok) {
            lv_obj_add_flag(wx_msg, LV_OBJ_FLAG_HIDDEN);
            wf_weather_icon(wx_iconc, code, WF_CX, 178);
            wf_draw_temp(wx_tempc, temp, 256, 15, 5);
            char b[40]; snprintf(b, sizeof b, "%d / %d", lo, hi);
            lv_label_set_text(wx_info, b);
        } else {
            lv_obj_remove_flag(wx_msg, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(wx_msg, "connect wifi");
            lv_label_set_text(wx_info, "");
        }
    }
}

static void weather_destroy(void) { wx_time = wx_iconc = wx_tempc = wx_info = wx_msg = NULL; }

/* ============================================================ 表盘注册表 ============================================================ */
static const watchface_t WF_DOTS    = { "dots",    dots_build,    dots_update,    dots_destroy    };
static const watchface_t WF_BOLD    = { "bold",    bold_build,    bold_update,    bold_destroy    };
static const watchface_t WF_RINGS   = { "rings",   rings_build,   rings_update,   rings_destroy   };
static const watchface_t WF_WEATHER = { "weather", weather_build, weather_update, weather_destroy };
static const watchface_t WF_IMAGE   = { "image",   image_build,   image_update,   image_destroy   };
static const watchface_t *const FACES[] = { &WF_DOTS, &WF_BOLD, &WF_RINGS, &WF_WEATHER, &WF_IMAGE };

/* ============================================================ 框架 ============================================================ */
static lv_obj_t *wf_screen, *wf_content, *wf_toast, *wf_lockdot;
static lv_timer_t *wf_timer;
static const watchface_t *cur_face;
static int  s_idx;
static bool s_aod;
static int  s_last_min = -1;

int         watchface_count(void)      { return (int)(sizeof(FACES) / sizeof(FACES[0])); }
const char *watchface_name(int idx)    { return (idx >= 0 && idx < watchface_count()) ? FACES[idx]->name : ""; }

static void wf_tick(lv_timer_t *t) {
    (void)t;
    if (!cur_face) return;
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    bool mc = (tm.tm_min != s_last_min);
    cur_face->update(&tm, s_aod, mc);
    if (mc) s_last_min = tm.tm_min;
}

void watchface_select(int idx) {
    int n = watchface_count();
    if (idx < 0) idx = 0;
    if (idx >= n) idx = n - 1;
    if (cur_face && cur_face->destroy) cur_face->destroy();
    lv_obj_clean(wf_content);
    s_idx = idx;
    cur_face = FACES[idx];
    cur_face->build(wf_content);
    s_last_min = -1;
    if (watchface_visible()) wf_tick(NULL);    // 立即按当前状态(活动/AOD)画一帧
}

static void toast_opa_cb(void *o, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)o, (lv_opa_t)v, 0); }

static void show_toast(const char *name) {
    lv_label_set_text(wf_toast, name);
    lv_anim_delete(wf_toast, toast_opa_cb);
    lv_obj_set_style_opa(wf_toast, LV_OPA_COVER, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, wf_toast);
    lv_anim_set_exec_cb(&a, toast_opa_cb);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_delay(&a, 800);
    lv_anim_set_duration(&a, 500);
    lv_anim_start(&a);
}

void watchface_next(int dir) {
    int n = watchface_count();
    int idx = (s_idx + dir + n) % n;
    watchface_select(idx);
    settings_set_face((uint8_t)idx);
    settings_save();
    show_toast(watchface_name(idx));
}

void watchface_set_aod(bool aod) {
    if (aod == s_aod) return;
    s_aod = aod;
    s_last_min = -1;                            // 强制按新状态重画(去掉/恢复闪烁与秒点)
    if (watchface_visible() && wf_timer) wf_tick(NULL);
}

static void wf_gesture_cb(lv_event_t *e) {
    (void)e;
    lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_active());
    if (d == LV_DIR_LEFT)       watchface_next(+1);
    else if (d == LV_DIR_RIGHT) watchface_next(-1);
}

void watchface_init(void) {
    wf_screen = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(wf_screen);
    lv_obj_set_size(wf_screen, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(wf_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(wf_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(wf_screen, LV_OBJ_FLAG_SCROLLABLE);
    // 关键:wf_screen 建在 lv_layer_top() 上(parent 非空),默认带 GESTURE_BUBBLE,会把手势继续
    // 冒泡到 layer_top,使挂在 wf_screen 上的手势回调(换表盘 / 上滑解锁)永远收不到。去掉它,
    // 手势就停在 wf_screen(子节点仍保留 GESTURE_BUBBLE,手势能从表盘内容冒上来)。
    lv_obj_remove_flag(wf_screen, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(wf_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(wf_screen, wf_gesture_cb, LV_EVENT_GESTURE, NULL);   // 左右滑换表盘

    wf_content = lv_obj_create(wf_screen);
    lv_obj_remove_style_all(wf_content);
    lv_obj_set_size(wf_content, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(wf_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(wf_content, LV_OBJ_FLAG_EVENT_BUBBLE);

    wf_lockdot = mkdot(wf_screen, WF_CX, 40, 3, COL_RED, LV_OPA_COVER);   // 顶端"已锁定"红点(各表盘共用)

    wf_toast = lv_label_create(wf_screen);
    lv_obj_set_style_text_font(wf_toast, UI_FONT_L, 0);
    lv_obj_set_style_text_color(wf_toast, lv_color_hex(COL_TXT), 0);
    lv_label_set_text(wf_toast, "");
    lv_obj_align(wf_toast, LV_ALIGN_BOTTOM_MID, 0, -52);
    lv_obj_set_style_opa(wf_toast, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(wf_toast, LV_OBJ_FLAG_EVENT_BUBBLE);

    cur_face = NULL;
    s_aod = false;
    watchface_select(settings_face());
}

void watchface_show(void) {
    if (!wf_screen) return;
    s_last_min = -1;
    lv_obj_remove_flag(wf_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(wf_screen);
    if (!wf_timer) wf_timer = lv_timer_create(wf_tick, 1000, NULL);
    wf_tick(NULL);
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
