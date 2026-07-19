// 蓝牙鼠标 app —— 手表变 BLE HID 鼠标(电脑/手机/平板原生支持,Just Works 配对)。
// 屏幕 = 触控板:拖动移光标,轻点 = 左键单击;底部两键 = 左/右键(可按住拖拽);
// 右缘竖条 = 滚轮。BLE 链路在 ble_hid.c(HOGP),退出即停广播断连(NimBLE host 常驻)。
#include "app.h"
#include "ble_hid.h"
#include "glyph.h"
#include <stdlib.h>

#define GAIN      1.6f     // 触控板位移增益
#define TAP_MS    220      // 轻点判定:按下时长 <
#define TAP_MOVE  10       // 轻点判定:总位移 <(px)
#define SEND_MS   15       // 报文最小间隔(约 66Hz)

static lv_obj_t *g_status, *g_pad;
static uint8_t   s_btns;                     // 当前按键位图(左/右键按住状态)
static lv_point_t s_last;                    // 触控板上次触点
static float     s_ax, s_ay;                 // 位移积累(亚像素)
static int       s_totmove;                  // 本次按下累计位移(轻点判定)
static uint32_t  s_press_tick, s_send_tick;
static int       s_wheel_lasty;              // 滚轮条上次 y
static float     s_wacc;                     // 滚轮积累
static bool      s_hid_started, s_link_state_known, s_was_connected;

static void send_now(uint8_t btns, int8_t dx, int8_t dy, int8_t wheel) {
    ble_hid_mouse(btns, dx, dy, wheel);
    s_send_tick = lv_tick_get();
}

/* ---- 触控板:拖动 = 移动,轻点 = 左键单击 ---- */
static void pad_event(lv_event_t *e) {
    lv_event_code_t c = lv_event_get_code(e);
    lv_indev_t *id = lv_indev_active();
    if (!id) return;
    lv_point_t p;
    lv_indev_get_point(id, &p);

    if (c == LV_EVENT_PRESSED) {
        s_last = p;
        s_ax = s_ay = 0;
        s_totmove = 0;
        s_press_tick = lv_tick_get();
    } else if (c == LV_EVENT_PRESSING) {
        int dx = p.x - s_last.x, dy = p.y - s_last.y;
        s_last = p;
        s_totmove += abs(dx) + abs(dy);
        s_ax += dx * GAIN;
        s_ay += dy * GAIN;
        if (lv_tick_elaps(s_send_tick) >= SEND_MS && ((int)s_ax || (int)s_ay)) {
            int mx = (int)s_ax, my = (int)s_ay;
            if (mx > 127) mx = 127; else if (mx < -127) mx = -127;
            if (my > 127) my = 127; else if (my < -127) my = -127;
            s_ax -= mx; s_ay -= my;
            send_now(s_btns, (int8_t)mx, (int8_t)my, 0);
        }
    } else if (c == LV_EVENT_RELEASED) {
        if (lv_tick_elaps(s_press_tick) < TAP_MS && s_totmove < TAP_MOVE) {
            send_now(s_btns | 0x01, 0, 0, 0);   // 轻点:左键按下
            send_now(s_btns, 0, 0, 0);          // 立即抬起
        }
    }
}

/* ---- 左/右键(按住可拖拽) ---- */
static void btn_event(lv_event_t *e) {
    uint8_t bit = (uint8_t)(intptr_t)lv_event_get_user_data(e);
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_PRESSED)       s_btns |= bit;
    else if (c == LV_EVENT_RELEASED) s_btns &= ~bit;
    else return;
    send_now(s_btns, 0, 0, 0);
}

/* ---- 右缘滚轮条 ---- */
static void wheel_event(lv_event_t *e) {
    lv_event_code_t c = lv_event_get_code(e);
    lv_indev_t *id = lv_indev_active();
    if (!id) return;
    lv_point_t p;
    lv_indev_get_point(id, &p);
    if (c == LV_EVENT_PRESSED) { s_wheel_lasty = p.y; s_wacc = 0; return; }
    if (c != LV_EVENT_PRESSING) return;
    s_wacc += (s_wheel_lasty - p.y) / 12.0f;    // 上滑 = 内容上滚(正)
    s_wheel_lasty = p.y;
    if (lv_tick_elaps(s_send_tick) >= SEND_MS && (int)s_wacc) {
        int w = (int)s_wacc;
        if (w > 7) w = 7; else if (w < -7) w = -7;
        s_wacc -= w;
        send_now(s_btns, 0, 0, (int8_t)w);
    }
}

static lv_obj_t *mk_btn(lv_obj_t *parent, const char *txt, int x, uint8_t bit) {
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, 108, 54);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x1c1c22), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x2a2a32), LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, 16, 0);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, x, -34);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_GESTURE_BUBBLE);      // 按住拖拽时的划动不触发返回
    lv_obj_add_event_cb(b, btn_event, LV_EVENT_PRESSED, (void *)(intptr_t)bit);
    lv_obj_add_event_cb(b, btn_event, LV_EVENT_RELEASED, (void *)(intptr_t)bit);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, UI_FONT_M, 0);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    return b;
}

static void mouse_enter(lv_obj_t *parent) {
    // 触控板(整屏,先建,在最底层)
    g_pad = lv_obj_create(parent);
    lv_obj_remove_style_all(g_pad);
    lv_obj_set_size(g_pad, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(g_pad, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_pad, LV_OBJ_FLAG_CLICKABLE);
    // ★吞掉手势:触控板上快速划动是正常鼠标操作,不能冒泡成"右滑退出 app"。
    //   退出本 app 只走顶部 ‹ 返回键。
    lv_obj_remove_flag(g_pad, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(g_pad, pad_event, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(g_pad, pad_event, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(g_pad, pad_event, LV_EVENT_RELEASED, NULL);

    // 中心点描点阵提示(触控板视觉)
    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 3; x++)
            glyph_dot(g_pad, 233 - 28 + x * 28, 210 - 28 + y * 28, 2, COL_TXT2);

    g_status = lv_label_create(parent);
    lv_obj_set_style_text_font(g_status, UI_FONT_M, 0);
    lv_obj_set_style_text_color(g_status, lv_color_hex(COL_TXT2), 0);
    lv_obj_align(g_status, LV_ALIGN_TOP_MID, 0, 88);
    lv_label_set_text(g_status, tr(S_STARTING));

    // 右缘滚轮条
    lv_obj_t *wheel = lv_obj_create(parent);
    lv_obj_remove_style_all(wheel);
    lv_obj_set_size(wheel, 44, 240);
    lv_obj_align(wheel, LV_ALIGN_RIGHT_MID, -8, -20);
    lv_obj_set_style_radius(wheel, 22, 0);
    lv_obj_set_style_bg_color(wheel, lv_color_hex(0x141418), 0);
    lv_obj_set_style_bg_opa(wheel, LV_OPA_COVER, 0);
    lv_obj_remove_flag(wheel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(wheel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(wheel, LV_OBJ_FLAG_GESTURE_BUBBLE);   // 滚动条上滑动同样不当返回手势
    lv_obj_add_event_cb(wheel, wheel_event, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(wheel, wheel_event, LV_EVENT_PRESSING, NULL);
    for (int i = 0; i < 3; i++)
        glyph_dot(wheel, 22, 60 + i * 60, 2, COL_TXT2);

    mk_btn(parent, "L", -62, 0x01);
    mk_btn(parent, "R", 62, 0x02);

    s_btns = 0; s_ax = s_ay = s_wacc = 0;
    s_link_state_known = false;               // 每次进入都强制回放 BLE 状态
    s_hid_started = ble_hid_start();
    if (!s_hid_started) lv_label_set_text(g_status, tr(S_BT_FAIL));
}

static void mouse_tick(void) {
    if (!g_status || !s_hid_started) return;
    bool now = ble_hid_connected();
    if (!s_link_state_known || now != s_was_connected) {
        s_link_state_known = true;
        s_was_connected = now;
        lv_label_set_text(g_status, tr(now ? S_CONNECTED : S_MOUSE_PAIR));
        lv_obj_set_style_text_color(g_status, lv_color_hex(now ? COL_CHARGE : COL_TXT2), 0);
    }
}

static void mouse_exit(void) {
    ble_hid_stop();                 // 停广播 + 断连(NimBLE host 常驻,teardown 纪律)
    g_status = g_pad = NULL;
    s_btns = 0;
    s_hid_started = s_link_state_known = false;
}

const app_t app_mouse = { "mouse", COL_TXT, mouse_enter, mouse_tick, mouse_exit };
