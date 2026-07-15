// 正向计时器(秒表)—— 从 0 累加。BOOT 实体键=开始/暂停/继续(不占锁屏侧键,防误触);
// 屏上按钮:运行=计圈(lap),停止=归零(reset)。秒环每分钟扫一圈;中心 MM:SS + 百分秒;顶部最近 3 圈。
#include "app.h"
#include "glyph.h"
#include "bootkey.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define CX      233
#define CY      233
#define RING_R  205
#define RING_N  60
#define RING_DR 3
#define CP      13
#define CDR     5
#define SW_OY   168           // 数字顶行 y
#define MAXLAP  30

/* 5×7 点阵字模在 glyph_font5x7[](glyph.c),各计时/表盘 app 共用 */

static bool      s_run;
static int64_t   s_base_us, s_start_us, s_laps[MAXLAP];
static int       s_nlap, s_last_sec = -1, s_last_ring = -1;
static lv_obj_t *g_ringbox, *g_ring[RING_N], *g_center, *g_cs, *g_hint, *g_lap, *g_btnl;

static int64_t elapsed_us(void) { return s_run ? s_base_us + (esp_timer_get_time() - s_start_us) : s_base_us; }

static lv_obj_t *mkdot(lv_obj_t *p, int x, int y, int r, uint32_t color, lv_opa_t opa) {
    lv_obj_t *d = lv_obj_create(p);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, r * 2, r * 2);
    lv_obj_set_pos(d, x - r, y - r);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(d, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(d, opa, 0);
    lv_obj_remove_flag(d, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(d, LV_OBJ_FLAG_EVENT_BUBBLE);
    return d;
}

static void draw_digit(lv_obj_t *par, char ch, int ox, int oy) {
    if (ch < '0' || ch > '9') return;
    const char *const *g = glyph_font5x7[ch - '0'];
    for (int r = 0; r < 7; r++)
        for (int c = 0; c < 5; c++)
            if (g[r][c] == '1') mkdot(par, ox + c * CP + CP / 2, oy + r * CP + CP / 2, CDR, COL_TXT, LV_OPA_COVER);
}

static void draw_mmss(int secs) {
    lv_obj_clean(g_center);
    char b[5]; snprintf(b, sizeof b, "%02d%02d", (secs / 60) % 100, secs % 60);
    int dw = 5 * CP, g = CP, colw = CP, x0 = CX - (4 * dw + 4 * g + colw) / 2, ox = x0;
    draw_digit(g_center, b[0], ox, SW_OY); ox += dw + g;
    draw_digit(g_center, b[1], ox, SW_OY); ox += dw + g;
    int ccx = ox + colw / 2; ox += colw + g;
    draw_digit(g_center, b[2], ox, SW_OY); ox += dw + g;
    draw_digit(g_center, b[3], ox, SW_OY);
    mkdot(g_center, ccx, SW_OY + 2 * CP + CP / 2, CDR, COL_RED, LV_OPA_COVER);
    mkdot(g_center, ccx, SW_OY + 4 * CP + CP / 2, CDR, COL_RED, LV_OPA_COVER);
}

static void draw_ring(int sec) {
    for (int i = 0; i < RING_N; i++) {
        lv_obj_set_style_bg_color(g_ring[i], lv_color_hex(i == sec ? COL_RED : COL_TXT), 0);
        lv_obj_set_style_bg_opa(g_ring[i], i <= sec ? LV_OPA_COVER : LV_OPA_20, 0);
    }
}

static void set_hint(const char *t, uint32_t col) {
    lv_label_set_text(g_hint, t);
    lv_obj_set_style_text_color(g_hint, lv_color_hex(col), 0);
}
static void update_btn(void) {
    lv_label_set_text(g_btnl, s_run ? "lap" : "reset");
    lv_obj_set_style_text_color(g_btnl, lv_color_hex(s_run ? COL_TXT : COL_RED), 0);
}
static void update_laps(void) {
    if (s_nlap == 0) { lv_label_set_text(g_lap, ""); return; }
    char buf[112] = "";
    int shown = 0;
    for (int k = s_nlap - 1; k >= 0 && shown < 3; k--, shown++) {     // 最近 3 圈,新的在上
        int64_t split = s_laps[k] - (k > 0 ? s_laps[k - 1] : 0);
        int s = (int)(split / 1000000), cs = (int)((split / 10000) % 100);
        char line[32]; snprintf(line, sizeof line, "%d   %02d:%02d.%02d\n", k + 1, s / 60, s % 60, cs);
        strcat(buf, line);
    }
    lv_label_set_text(g_lap, buf);
}

static void toggle_run(void) {             // BOOT 键:开始 / 暂停 / 继续
    if (s_run) { s_base_us += esp_timer_get_time() - s_start_us; s_run = false; set_hint("paused - boot key = resume", COL_TXT2); }
    else       { s_start_us = esp_timer_get_time(); s_run = true; set_hint("running - btn = lap", COL_TXT2); }
    update_btn();
}
static void btn_cb(lv_event_t *e) {        // 屏上按钮:运行=计圈,停止=归零
    if (s_run) { if (s_nlap < MAXLAP) s_laps[s_nlap++] = elapsed_us(); update_laps(); }
    else {
        s_base_us = 0; s_nlap = 0; s_last_sec = -1; s_last_ring = -1;
        update_laps(); set_hint("boot key = start", COL_TXT2);
    }
}

static void stopwatch_enter(lv_obj_t *parent) {
    g_ringbox = lv_obj_create(parent);
    lv_obj_remove_style_all(g_ringbox);
    lv_obj_set_size(g_ringbox, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(g_ringbox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_ringbox, LV_OBJ_FLAG_EVENT_BUBBLE);
    for (int i = 0; i < RING_N; i++) {
        float a = i / (float)RING_N * 6.2832f - 1.5708f;
        g_ring[i] = mkdot(g_ringbox, CX + (int)(cosf(a) * RING_R), CY + (int)(sinf(a) * RING_R), RING_DR, COL_TXT, LV_OPA_20);
    }

    g_hint = lv_label_create(parent);
    lv_obj_set_style_text_font(g_hint, UI_FONT_M, 0);
    lv_obj_set_style_text_align(g_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_hint, LV_ALIGN_TOP_MID, 0, 86);
    set_hint("boot key = start", COL_TXT2);

    g_lap = lv_label_create(parent);          // 最近圈
    lv_obj_set_style_text_font(g_lap, UI_FONT_M, 0);
    lv_obj_set_style_text_color(g_lap, lv_color_hex(0x55555a), 0);
    lv_obj_set_style_text_align(g_lap, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(g_lap, "");
    lv_obj_align(g_lap, LV_ALIGN_TOP_MID, 0, 110);

    g_center = lv_obj_create(parent);
    lv_obj_remove_style_all(g_center);
    lv_obj_set_size(g_center, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(g_center, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_center, LV_OBJ_FLAG_EVENT_BUBBLE);

    g_cs = lv_label_create(parent);           // 百分秒
    lv_obj_set_style_text_font(g_cs, UI_FONT_L, 0);
    lv_obj_set_style_text_color(g_cs, lv_color_hex(COL_TXT2), 0);
    lv_obj_align(g_cs, LV_ALIGN_TOP_MID, 0, 282);

    lv_obj_t *btn = lv_button_create(parent); // lap / reset
    lv_obj_set_size(btn, 120, 46);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1c1c22), 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 322);
    lv_obj_add_event_cb(btn, btn_cb, LV_EVENT_CLICKED, NULL);
    g_btnl = lv_label_create(btn);
    lv_obj_center(g_btnl);

    bootkey_init();                           // BOOT 键 = 开始/暂停/继续
    s_run = false; s_base_us = 0; s_nlap = 0; s_last_sec = -1; s_last_ring = -1;
    update_btn();
    update_laps();
}

static void stopwatch_tick(void) {
    if (!g_center) return;
    if (bootkey_pressed()) toggle_run();      // BOOT 键:开始/暂停/继续
    int64_t e = elapsed_us();
    int sec = (int)(e / 1000000);
    if (sec != s_last_sec) { draw_mmss(sec); s_last_sec = sec; }
    int rs = sec % 60;
    if (rs != s_last_ring) { draw_ring(rs); s_last_ring = rs; }
    char b[8]; snprintf(b, sizeof b, ".%02d", (int)((e / 10000) % 100));
    lv_label_set_text(g_cs, b);
}

static void stopwatch_exit(void) {
    g_ringbox = g_center = g_cs = g_hint = g_lap = g_btnl = NULL;
    s_last_sec = s_last_ring = -1;
}

const app_t app_stopwatch = { "stopwatch", COL_TXT, stopwatch_enter, stopwatch_tick, stopwatch_exit };
