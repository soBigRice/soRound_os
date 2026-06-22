// 倒计时 app —— 圆形递减点环(从 12 点顺时针变暗)+ 中心 MM:SS 大点阵。
// 一触式预设(1/3/5/10/25 分)立即开始;点中心暂停/继续,reset 重置;归零中心红字闪烁。
// Nothing 单色 + 唯一红强调。时间用 esp_timer 计(暂停/继续不丢精度)。
#include "app.h"
#include "audio_out.h"
#include "esp_timer.h"
#include <stdio.h>
#include <math.h>

#define CX      233
#define CY      233
#define RING_R  205
#define RING_N  60
#define RING_DR 3
#define CP      13        // 数字点距
#define CDR     5         // 数字点半径

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

typedef enum { ST_IDLE, ST_RUN, ST_PAUSE, ST_DONE } cd_state_t;
static cd_state_t s_state;
static int64_t    s_end_us;       // RUN:结束时刻
static int64_t    s_remain_us;    // PAUSE:剩余
static int        s_total_s = 300; // 选定总秒(默认 5:00,可 ± 自定义)
static int        s_last_shown = -1, s_last_remn = -1;
static bool       s_blink_on;
static int        s_blink_div;
static int        s_alarm_div;    // DONE 态每 ~3s 再响一次

static lv_obj_t *g_ringbox, *g_ring[RING_N], *g_center, *g_centerbtn, *g_hint, *g_reset, *g_idle;

static const int PRESETS[] = { 1, 3, 5, 10, 25 };
#define NPRE (int)(sizeof(PRESETS) / sizeof(PRESETS[0]))

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

static void vis(lv_obj_t *o, bool on) {
    if (!o) return;
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void draw_digit(lv_obj_t *par, char ch, int ox, int oy, uint32_t col) {
    if (ch < '0' || ch > '9') return;
    const char *const *g = DIGITS[ch - '0'];
    for (int r = 0; r < 7; r++)
        for (int c = 0; c < 5; c++)
            if (g[r][c] == '1') mkdot(par, ox + c * CP + CP / 2, oy + r * CP + CP / 2, CDR, col, LV_OPA_COVER);
}

static void draw_mmss(int secs) {
    lv_obj_clean(g_center);
    char b[5]; snprintf(b, sizeof b, "%02d%02d", secs / 60, secs % 60);
    uint32_t col = (s_state == ST_DONE) ? (s_blink_on ? COL_RED : COL_BG) : COL_TXT;
    int dw = 5 * CP, g = CP, colw = CP;
    int total = 4 * dw + 4 * g + colw;
    int x0 = CX - total / 2, oy = CY - (7 * CP) / 2 - 24, ox = x0;
    draw_digit(g_center, b[0], ox, oy, col); ox += dw + g;
    draw_digit(g_center, b[1], ox, oy, col); ox += dw + g;
    int ccx = ox + colw / 2; ox += colw + g;
    draw_digit(g_center, b[2], ox, oy, col); ox += dw + g;
    draw_digit(g_center, b[3], ox, oy, col);
    mkdot(g_center, ccx, oy + 2 * CP + CP / 2, CDR, col, LV_OPA_COVER);   // 冒号
    mkdot(g_center, ccx, oy + 4 * CP + CP / 2, CDR, col, LV_OPA_COVER);
}

// remainN:剩余点数。已耗的点从 12 点顺时针变暗,前沿一个红点
static void draw_ring(int remainN) {
    int elapsed = RING_N - remainN;
    for (int i = 0; i < RING_N; i++) {
        bool gone = (i < elapsed);
        lv_obj_set_style_bg_color(g_ring[i], lv_color_hex(COL_TXT), 0);
        lv_obj_set_style_bg_opa(g_ring[i], gone ? LV_OPA_20 : LV_OPA_COVER, 0);
    }
    if (elapsed < RING_N) lv_obj_set_style_bg_color(g_ring[elapsed], lv_color_hex(COL_RED), 0);   // 前沿红点
}

static void show_state(cd_state_t st) {
    s_state = st;
    bool idle = (st == ST_IDLE);
    vis(g_idle, idle);                 // 设定面板(±/预设/提示)仅 IDLE
    vis(g_ringbox, !idle);             // 递减环仅运行态
    vis(g_hint, !idle);
    vis(g_reset, !idle);
    vis(g_center, true);               // 计时区两态都显示(IDLE 显示待设定时间)
    vis(g_centerbtn, true);            // 点计时区:IDLE=开始 / RUN=暂停 / PAUSE=继续 / DONE=重置
    if (idle) {
        draw_mmss(s_total_s);          // 显示当前选择
    } else {
        const char *h = (st == ST_RUN) ? "tap to pause" : (st == ST_PAUSE) ? "paused - tap to resume" : "done - tap to reset";
        lv_label_set_text(g_hint, h);
        lv_obj_set_style_text_color(g_hint, lv_color_hex(st == ST_DONE ? COL_RED : COL_TXT2), 0);
    }
}

static void begin_run(int total_s) {
    s_total_s = total_s;
    s_end_us = esp_timer_get_time() + (int64_t)total_s * 1000000;
    s_last_shown = -1; s_last_remn = -1;
    show_state(ST_RUN);
}

static void preset_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e));
    begin_run(PRESETS[idx] * 60);
}

static void center_cb(lv_event_t *e) {
    int64_t now = esp_timer_get_time();
    if (s_state == ST_IDLE) { if (s_total_s >= 5) begin_run(s_total_s); return; }   // 点计时区 → 用自定义时间开始
    if (s_state == ST_RUN) {
        s_remain_us = s_end_us - now; if (s_remain_us < 0) s_remain_us = 0;
        show_state(ST_PAUSE);
    } else if (s_state == ST_PAUSE) {
        s_end_us = now + s_remain_us;
        s_last_shown = -1; s_last_remn = -1;
        show_state(ST_RUN);
    } else if (s_state == ST_DONE) {
        show_state(ST_IDLE);
    }
}

static void reset_cb(lv_event_t *e) { show_state(ST_IDLE); }

static void clamp_total(void) {
    if (s_total_s < 5) s_total_s = 5;                         // 最少 5 秒
    if (s_total_s > 99 * 60 + 59) s_total_s = 99 * 60 + 59;   // 最多 99:59
}
static void step_cb(lv_event_t *e) {                          // ±1m / ±10s(按住连发)
    int k = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e));
    s_total_s += (k == 0) ? -60 : (k == 1) ? 60 : (k == 2) ? -10 : 10;
    clamp_total();
    draw_mmss(s_total_s);
}

static lv_obj_t *mk_button(lv_obj_t *par, const char *txt, int w, int h, uint32_t bg, lv_event_cb_t cb) {
    lv_obj_t *b = lv_button_create(par);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_radius(b, 12, 0);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_center(l);
    return b;
}

static void countdown_enter(lv_obj_t *parent) {
    audio_out_init();              // 起扬声器(闹铃);退出时释放,把 I2S0 让回麦克风
    // 递减点环
    g_ringbox = lv_obj_create(parent);
    lv_obj_remove_style_all(g_ringbox);
    lv_obj_set_size(g_ringbox, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(g_ringbox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_ringbox, LV_OBJ_FLAG_EVENT_BUBBLE);
    for (int i = 0; i < RING_N; i++) {
        float a = i / (float)RING_N * 6.2832f - 1.5708f;
        g_ring[i] = mkdot(g_ringbox, CX + (int)(cosf(a) * RING_R), CY + (int)(sinf(a) * RING_R),
                          RING_DR, COL_TXT, LV_OPA_COVER);
    }

    // 中心 MM:SS 容器
    g_center = lv_obj_create(parent);
    lv_obj_remove_style_all(g_center);
    lv_obj_set_size(g_center, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(g_center, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_center, LV_OBJ_FLAG_EVENT_BUBBLE);

    g_hint = lv_label_create(parent);
    lv_obj_set_style_text_font(g_hint, UI_FONT_M, 0);
    lv_obj_set_style_text_color(g_hint, lv_color_hex(COL_TXT2), 0);
    lv_label_set_text(g_hint, "");
    lv_obj_align(g_hint, LV_ALIGN_TOP_MID, 0, 292);

    // 计时区透明点击区(只盖时间数字,不挡下方 ±/预设):IDLE=开始,RUN/PAUSE=暂停/继续,DONE=重置
    g_centerbtn = lv_obj_create(parent);
    lv_obj_remove_style_all(g_centerbtn);
    lv_obj_set_size(g_centerbtn, 240, 104);
    lv_obj_align(g_centerbtn, LV_ALIGN_TOP_MID, 0, 150);
    lv_obj_remove_flag(g_centerbtn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_centerbtn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_centerbtn, center_cb, LV_EVENT_CLICKED, NULL);

    g_reset = mk_button(parent, "reset", 120, 46, 0x1c1c22, reset_cb);
    lv_obj_set_style_text_color(lv_obj_get_child(g_reset, 0), lv_color_hex(COL_RED), 0);   // 红字强调,与全局风格一致
    lv_obj_align(g_reset, LV_ALIGN_TOP_MID, 0, 332);   // 上移到下半圆内,别贴底边

    // IDLE 设定面板:容器本身不可点(让计时区的点击穿透到 g_centerbtn 去"开始"),按钮各自可点
    g_idle = lv_obj_create(parent);
    lv_obj_remove_style_all(g_idle);
    lv_obj_set_size(g_idle, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(g_idle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(g_idle, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *tip = lv_label_create(g_idle);
    lv_obj_set_style_text_font(tip, UI_FONT_M, 0);
    lv_obj_set_style_text_color(tip, lv_color_hex(COL_TXT2), 0);
    lv_label_set_text(tip, "tap time to start");
    lv_obj_align(tip, LV_ALIGN_TOP_MID, 0, 96);

    // 自定义:±1m / ±10s(按住连发)
    static const char *const SLAB[4] = { "-1m", "+1m", "-10s", "+10s" };
    int sw = 62, sg = 8, sx0 = (466 - (4 * sw + 3 * sg)) / 2;
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = mk_button(g_idle, SLAB[i], sw, 44, 0x1c1c22, NULL);
        lv_obj_set_user_data(b, (void *)(intptr_t)i);
        lv_obj_add_event_cb(b, step_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(b, step_cb, LV_EVENT_LONG_PRESSED_REPEAT, NULL);
        lv_obj_set_pos(b, sx0 + i * (sw + sg), 272);
    }

    // 快捷预设(分钟):点了立即开始
    int bw = 56, bg = 10, px0 = (466 - (NPRE * bw + (NPRE - 1) * bg)) / 2;
    for (int i = 0; i < NPRE; i++) {
        char t[6]; snprintf(t, sizeof t, "%d", PRESETS[i]);
        lv_obj_t *b = mk_button(g_idle, t, bw, 46, 0x1c1c22, preset_cb);
        lv_obj_set_user_data(b, (void *)(intptr_t)i);
        lv_obj_set_pos(b, px0 + i * (bw + bg), 330);
    }

    s_state = ST_IDLE;
    show_state(ST_IDLE);
}

static void countdown_tick(void) {
    if (!g_center) return;
    if (s_state == ST_RUN) {
        int64_t us = s_end_us - esp_timer_get_time();
        if (us <= 0) {
            s_blink_on = true; s_blink_div = 0; s_alarm_div = 0;
            show_state(ST_DONE);
            draw_ring(0);
            s_last_shown = 0; draw_mmss(0);
            audio_out_alarm();                                   // 时间到 → 闹铃(静音模式不响)
            return;
        }
        int left = (int)((us + 999999) / 1000000);            // 向上取整
        if (left != s_last_shown) { draw_mmss(left); s_last_shown = left; }
        int remn = (int)ceilf((float)us / 1e6f / s_total_s * RING_N);
        if (remn > RING_N) remn = RING_N;
        if (remn != s_last_remn) { draw_ring(remn); s_last_remn = remn; }
    } else if (s_state == ST_DONE) {
        if (++s_blink_div >= 10) { s_blink_div = 0; s_blink_on = !s_blink_on; draw_mmss(0); }   // ~2Hz 闪
        if (++s_alarm_div >= 60) { s_alarm_div = 0; audio_out_alarm(); }                        // ~3s 再响
    }
}

static void countdown_exit(void) {
    audio_out_deinit();
    g_ringbox = g_center = g_centerbtn = g_hint = g_reset = g_idle = NULL;
    s_last_shown = s_last_remn = -1;
}

const app_t app_countdown = { "countdown", COL_TXT, countdown_enter, countdown_tick, countdown_exit };
