// 遥控台:保留触控鼠标,同一 BLE 连接内切换演示翻页和媒体控制。
// 拖拽锁定让单指触摸也可按住左键移动;遮挡/断连/模式切换统一释放输入。
#include "app.h"
#include "ble_hid.h"
#include "glyph.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "remote_media.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "remote_icons.inc"
#include <stdlib.h>
#include <stdio.h>

#define GAIN 1.6f
#define TAP_MS 220
#define TAP_MOVE 10
#define SEND_MS 15

static lv_obj_t *g_parent, *g_body, *g_status, *g_pad, *g_drag, *g_time;
static lv_obj_t *g_modes[HID_REPORT_COUNT], *g_controls[8];
static int s_control_count, s_status = -1;
static hid_report_t s_mode;
static bool s_started, s_visible, s_ready, s_drag;
static uint8_t s_btns;
static lv_point_t s_last;
static float s_ax, s_ay, s_wacc;
static int s_totmove, s_wheel_lasty;
static uint32_t s_press_tick, s_send_tick;
static int64_t s_feedback_until, s_elapsed_us, s_timer_since;
static bool s_timer_running;
static unsigned s_shown_seconds;
static lv_obj_t *g_media_layer, *g_cover, *g_overlay, *g_media_hint, *g_play_symbol, *g_volume_panel;
static uint8_t *s_cover_pixels, *s_overlay_pixels;
static lv_image_dsc_t s_cover_image, s_overlay_image;
static uint32_t s_cover_revision, s_overlay_revision, s_rotation_at;
static int32_t s_rotation;
static bool s_volume_open;

static void update_connection(void);
static void build_mode(void);

static void free_media_view(void) {
    // LVGL 必须先丢掉图像对象与缓存，再释放 UI 持有的 PSRAM 副本。
    // Delete image objects/cache before releasing the UI-owned PSRAM copies.
    if (g_media_layer) lv_obj_delete(g_media_layer);
    g_media_layer = g_cover = g_overlay = g_media_hint = g_play_symbol = g_volume_panel = NULL;
    lv_image_cache_drop(&s_cover_image); lv_image_cache_drop(&s_overlay_image);
    heap_caps_free(s_cover_pixels); heap_caps_free(s_overlay_pixels);
    s_cover_pixels = s_overlay_pixels = NULL;
    s_cover_revision = s_overlay_revision = 0; s_rotation = 0; s_volume_open = false;
}

static void update_media_view(void) {
    if (s_mode != HID_MEDIA || !g_cover) return;
    remote_media_snapshot_t state;
    remote_media_get_snapshot(&state);
    bool artwork = false, overlay = false;
    if (state.connected && state.has_artwork) {
        if (!s_cover_pixels) s_cover_pixels = heap_caps_malloc(REMOTE_MEDIA_ARTWORK_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_cover_pixels && s_cover_revision != state.artwork_revision &&
            remote_media_copy_artwork(s_cover_pixels, REMOTE_MEDIA_ARTWORK_BYTES, state.artwork_revision)) {
            lv_image_cache_drop(&s_cover_image);
            s_cover_image = (lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,
                .w=466,.h=466,.stride=932},.data=s_cover_pixels,.data_size=REMOTE_MEDIA_ARTWORK_BYTES};
            lv_image_set_src(g_cover, &s_cover_image); s_cover_revision = state.artwork_revision;
        }
        artwork = s_cover_pixels && s_cover_revision == state.artwork_revision;
    }
    if (state.connected && state.has_overlay) {
        if (!s_overlay_pixels) s_overlay_pixels = heap_caps_malloc(REMOTE_MEDIA_OVERLAY_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_overlay_pixels && s_overlay_revision != state.overlay_revision &&
            remote_media_copy_overlay(s_overlay_pixels, REMOTE_MEDIA_OVERLAY_BYTES, state.overlay_revision)) {
            lv_image_cache_drop(&s_overlay_image);
            s_overlay_image = (lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_A8,
                .w=350,.h=180,.stride=350},.data=s_overlay_pixels,.data_size=REMOTE_MEDIA_OVERLAY_BYTES};
            lv_image_set_src(g_overlay, &s_overlay_image); s_overlay_revision = state.overlay_revision;
        }
        overlay = s_overlay_pixels && s_overlay_revision == state.overlay_revision;
    }
    ui_obj_set_hidden(g_cover, !artwork); ui_obj_set_hidden(g_overlay, !overlay);
    ui_obj_set_hidden(g_media_hint, overlay);
    // 只依据主机回传状态选择图标；HID 单向模式始终显示播放/暂停复合符号。
    // Only host feedback selects play versus pause; one-way HID keeps a toggle symbol.
    lv_label_set_text(g_play_symbol, state.connected ? (state.playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY) : LV_SYMBOL_PLAY " " LV_SYMBOL_PAUSE);
    uint32_t now = lv_tick_get();
    if (s_visible && artwork && state.playing && lv_tick_elaps(s_rotation_at) >= 100) {
        s_rotation = (s_rotation + 3) % 3600;
        lv_image_set_rotation(g_cover, s_rotation); s_rotation_at = now;
    } else if (!s_visible || !artwork || !state.playing) s_rotation_at = now;
}

static void volume_event(lv_event_t *e) {
    (void)e;
    if (s_mode != HID_MEDIA || !g_volume_panel) return;
    s_volume_open = !s_volume_open;
    ui_obj_set_hidden(g_volume_panel, !s_volume_open);
}

static uint8_t mouse_buttons(void) { return s_btns | (s_drag ? 1 : 0); }
static bool send_now(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel) {
    bool sent = ble_hid_mouse(buttons, dx, dy, wheel);
    s_send_tick = lv_tick_get();
    return sent;
}

static void clear_input(void) {
    ble_hid_release_all();
    s_btns = 0; s_drag = false;
    s_ax = s_ay = s_wacc = 0;
    s_totmove = TAP_MOVE; // 清理后到来的 RELEASED 不应被当成新轻点。
    if (g_drag) lv_obj_remove_state(g_drag, LV_STATE_CHECKED);
}

static void pad_event(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *id = lv_indev_active();
    if (!id || !s_ready || !s_visible) return;
    lv_point_t p; lv_indev_get_point(id, &p);
    if (code == LV_EVENT_PRESSED) {
        s_last = p; s_ax = s_ay = 0; s_totmove = 0; s_press_tick = lv_tick_get();
    } else if (code == LV_EVENT_PRESSING) {
        int dx = p.x - s_last.x, dy = p.y - s_last.y;
        s_last = p; s_totmove += abs(dx) + abs(dy);
        s_ax += dx * GAIN; s_ay += dy * GAIN;
        if (lv_tick_elaps(s_send_tick) >= SEND_MS && ((int)s_ax || (int)s_ay)) {
            int mx = (int)s_ax, my = (int)s_ay;
            if (mx > 127) mx = 127; else if (mx < -127) mx = -127;
            if (my > 127) my = 127; else if (my < -127) my = -127;
            if (send_now(mouse_buttons(), mx, my, 0)) { s_ax -= mx; s_ay -= my; }
        }
    } else if (code == LV_EVENT_RELEASED) {
        if (!s_drag && lv_tick_elaps(s_press_tick) < TAP_MS && s_totmove < TAP_MOVE) {
            if (send_now(mouse_buttons() | 1, 0, 0, 0)) send_now(mouse_buttons(), 0, 0, 0);
        }
    } else if (code == LV_EVENT_PRESS_LOST) {
        s_ax = s_ay = 0; s_totmove = TAP_MOVE;
    }
}

static void mouse_button_event(lv_event_t *e) {
    if (s_mode != HID_MOUSE) return;
    uint8_t bit = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        if (!s_ready || !s_visible) return;
        s_btns |= bit;
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) s_btns &= ~bit;
    else return;
    send_now(mouse_buttons(), 0, 0, 0);
}

static void drag_event(lv_event_t *e) {
    (void)e;
    if (!s_ready || !s_visible) return;
    s_drag = !s_drag;
    if (!send_now(mouse_buttons(), 0, 0, 0)) s_drag = false;
    if (s_drag) lv_obj_add_state(g_drag, LV_STATE_CHECKED);
    else lv_obj_remove_state(g_drag, LV_STATE_CHECKED);
}

static void wheel_event(lv_event_t *e) {
    lv_indev_t *id = lv_indev_active();
    if (!id || !s_ready || !s_visible) return;
    lv_point_t p; lv_indev_get_point(id, &p);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) { s_wheel_lasty = p.y; s_wacc = 0; }
    else if (code == LV_EVENT_PRESSING) {
        s_wacc += (s_wheel_lasty - p.y) / 12.0f; s_wheel_lasty = p.y;
        if (lv_tick_elaps(s_send_tick) >= SEND_MS && (int)s_wacc) {
            int amount = (int)s_wacc;
            if (amount > 7) amount = 7; else if (amount < -7) amount = -7;
            if (send_now(mouse_buttons(), 0, 0, amount)) s_wacc -= amount;
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) s_wacc = 0;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(COL_TXT), 0);
    lv_label_set_text(l, text);
    return l;
}

static lv_obj_t *button(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, w, h); lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x1c1c22), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x303036), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(b, lv_color_hex(COL_RED), LV_STATE_CHECKED);
    lv_obj_set_style_radius(b, 18, 0); lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_shadow_width(b, 0, 0); lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_set_style_opa(b, LV_OPA_40, LV_STATE_DISABLED);
    ui_obj_set_gesture_bubble(b, false);
    return b;
}

static void remote_action(lv_event_t *e) {
    if (!s_ready || !s_visible) return;
    uint16_t usage = (uint16_t)(uintptr_t)lv_event_get_user_data(e);
    bool accepted = s_mode == HID_KEYBOARD ? ble_hid_key_tap((uint8_t)usage) : ble_hid_media_tap(usage);
    if (!accepted) { s_feedback_until = esp_timer_get_time() + 800000; update_connection(); }
}

static lv_obj_t *action(int x, int y, int w, int h, const char *symbol, const char *text, uint16_t usage) {
    lv_obj_t *b = button(g_body, x, y, w, h);
    if (symbol) {
        lv_obj_t *icon = label(b, symbol, UI_FONT_SYM);
        lv_obj_align(icon, LV_ALIGN_CENTER, 0, text ? -13 : 0);
    }
    if (text) {
        lv_obj_t *l = label(b, text, UI_FONT_M);
        lv_obj_set_width(l, w - 8);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(l, LV_ALIGN_CENTER, 0, symbol ? 19 : 0);
    }
    lv_obj_add_event_cb(b, remote_action, LV_EVENT_CLICKED, (void *)(uintptr_t)usage);
    g_controls[s_control_count++] = b;
    return b;
}

static void timer_event(lv_event_t *e) {
    (void)e;
    int64_t now = esp_timer_get_time();
    if (s_timer_running) s_elapsed_us += now - s_timer_since;
    else s_timer_since = now;
    s_timer_running = !s_timer_running;
    lv_obj_set_style_text_color(g_time, lv_color_hex(s_timer_running ? COL_TXT : COL_TXT2), 0);
}

static void timer_reset(lv_event_t *e) {
    (void)e;
    s_elapsed_us = 0; s_timer_running = false; s_shown_seconds = 0;
    lv_label_set_text(g_time, "00:00");
    lv_obj_set_style_text_color(g_time, lv_color_hex(COL_TXT2), 0);
}

static void mode_event(lv_event_t *e) {
    hid_report_t mode = (hid_report_t)(uintptr_t)lv_event_get_user_data(e);
    if (mode == s_mode) return;
    clear_input(); s_mode = mode; s_feedback_until = 0; build_mode();
}

static void build_mode(void) {
    free_media_view();
    g_pad = g_drag = g_time = NULL;
    if (g_body) lv_obj_delete(g_body);
    g_body = lv_obj_create(g_parent); lv_obj_remove_style_all(g_body);
    lv_obj_set_pos(g_body, 0, 126); lv_obj_set_size(g_body, 466, 254);
    ui_obj_set_scrollable(g_body, false);
    ui_obj_set_clickable(g_body, false);
    s_control_count = 0;
    for (int i = 0; i < HID_REPORT_COUNT; i++) {
        if ((hid_report_t)i == s_mode) lv_obj_add_state(g_modes[i], LV_STATE_CHECKED);
        else lv_obj_remove_state(g_modes[i], LV_STATE_CHECKED);
    }
    if (s_mode == HID_MOUSE) {
        g_pad = lv_obj_create(g_body); lv_obj_remove_style_all(g_pad);
        lv_obj_set_size(g_pad, 466, 244);
        ui_obj_set_scrollable(g_pad, false);
        ui_obj_set_gesture_bubble(g_pad, false);
        ui_obj_set_clickable(g_pad, true);
        lv_obj_add_event_cb(g_pad, pad_event, LV_EVENT_ALL, NULL);
        g_controls[s_control_count++] = g_pad;
        lv_obj_set_height(g_pad, 174);
        for (int y = 0; y < 3; y++) for (int x = 0; x < 3; x++)
            glyph_dot(g_pad, 221 + x * 12, 44 + y * 12, 2, 0x929294);
        lv_obj_t *hint = label(g_pad, tr(S_REMOTE_PAD_HINT), UI_FONT_M);
        lv_obj_set_style_text_color(hint, lv_color_hex(0x929294), 0);
        lv_obj_set_width(hint, 310); lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(hint, LV_ALIGN_TOP_MID, -12, 92);
        lv_obj_t *wheel = button(g_body, 372, 12, 42, 152);
        lv_obj_set_style_radius(wheel, LV_RADIUS_CIRCLE, 0);
        lv_obj_add_event_cb(wheel, wheel_event, LV_EVENT_ALL, NULL);
        for (int i = 0; i < 3; i++) glyph_dot(wheel, 21, 28 + i * 48, 2, 0x929294);
        g_controls[s_control_count++] = wheel;
        for (int i = 0; i < 2; i++) {
            lv_obj_t *b = button(g_body, i ? 298 : 92, 174, 76, 76);
            lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(b, 1, 0);
            lv_obj_set_style_border_color(b, lv_color_hex(0x353a40), 0);
            lv_obj_t *icon = lv_image_create(b); lv_image_set_src(icon, &remote_mouse_icon);
            lv_obj_set_style_image_recolor(icon, lv_color_hex(COL_TXT), 0);
            lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, -12);
            lv_obj_t *name = label(b, tr(i ? S_REMOTE_RIGHT : S_REMOTE_LEFT), UI_FONT_SYM);
            lv_obj_align(name, LV_ALIGN_CENTER, 0, 23);
            lv_obj_add_event_cb(b, mouse_button_event, LV_EVENT_ALL, (void *)(uintptr_t)(i ? 2 : 1));
            g_controls[s_control_count++] = b;
        }
        g_drag = button(g_body, 195, 174, 76, 76);
        lv_obj_set_style_radius(g_drag, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(g_drag, 1, 0);
        lv_obj_set_style_border_color(g_drag, lv_color_hex(0x353a40), 0);
        lv_obj_t *lock = lv_image_create(g_drag); lv_image_set_src(lock, &remote_lock_icon);
        lv_obj_set_style_image_recolor(lock, lv_color_hex(COL_TXT), 0);
        lv_obj_set_style_image_recolor_opa(lock, LV_OPA_COVER, 0); lv_obj_align(lock, LV_ALIGN_CENTER, 0, -12);
        lv_obj_t *name = label(g_drag, tr(S_REMOTE_DRAG), UI_FONT_SYM); lv_obj_align(name, LV_ALIGN_CENTER, 0, 23);
        lv_obj_add_event_cb(g_drag, drag_event, LV_EVENT_CLICKED, NULL);
        g_controls[s_control_count++] = g_drag;
    } else if (s_mode == HID_KEYBOARD) {
        lv_obj_t *time_button = button(g_body, 113, 0, 240, 70);
        lv_obj_set_style_bg_opa(time_button, LV_OPA_TRANSP, 0);
        g_time = label(time_button, "00:00", &lv_font_montserrat_40); lv_obj_center(g_time);
        lv_obj_set_style_text_color(g_time, lv_color_hex(s_timer_running ? COL_TXT : COL_TXT2), 0);
        lv_obj_add_event_cb(time_button, timer_event, LV_EVENT_CLICKED, NULL);
        s_shown_seconds = UINT32_MAX;
        lv_obj_t *hint = label(g_body, tr(S_REMOTE_TIMER_HINT), UI_FONT_M);
        lv_obj_set_style_text_color(hint, lv_color_hex(COL_TXT2), 0);
        lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 77);
        action(108, 108, 112, 82, LV_SYMBOL_LEFT, tr(S_REMOTE_PREV_PAGE), HID_KEY_PAGE_UP);
        action(246, 108, 112, 82, LV_SYMBOL_RIGHT, tr(S_REMOTE_NEXT_PAGE), HID_KEY_PAGE_DOWN);
        lv_obj_t *reset = button(g_body, 190, 202, 86, 32);
        lv_obj_center(label(reset, tr(S_RESET), UI_FONT_M));
        lv_obj_add_event_cb(reset, timer_reset, LV_EVENT_CLICKED, NULL);
    } else {
        g_media_layer = lv_obj_create(g_parent); lv_obj_remove_style_all(g_media_layer);
        lv_obj_set_size(g_media_layer, 466, 466); lv_obj_set_pos(g_media_layer, 0, 0);
        ui_obj_set_scrollable(g_media_layer, false); ui_obj_set_clickable(g_media_layer, false);
        lv_obj_set_style_radius(g_media_layer, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_clip_corner(g_media_layer, true, 0); lv_obj_move_background(g_media_layer);
        g_cover = lv_image_create(g_media_layer); lv_obj_set_pos(g_cover, 0, 0);
        lv_image_set_pivot(g_cover, 233, 233); lv_obj_set_style_image_opa(g_cover, 160, 0);
        ui_obj_set_hidden(g_cover, true);
        g_overlay = lv_image_create(g_media_layer); lv_obj_set_pos(g_overlay, 58, 130);
        lv_obj_set_style_image_recolor(g_overlay, lv_color_hex(COL_TXT), 0);
        lv_obj_set_style_image_recolor_opa(g_overlay, LV_OPA_COVER, 0); ui_obj_set_hidden(g_overlay, true);
        g_media_hint = label(g_body, tr(S_REMOTE_SYSTEM_PLAYBACK), UI_FONT_SYM);
        lv_obj_set_height(g_media_hint, 110);
        lv_obj_set_width(g_media_hint, 310); lv_obj_set_style_text_align(g_media_hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(g_media_hint, LV_ALIGN_TOP_MID, 0, 44);
        lv_obj_t *waiting = label(g_media_hint, tr(S_REMOTE_WAIT_INFO), UI_FONT_M);
        lv_obj_set_style_text_color(waiting, lv_color_hex(0x929294), 0);
        lv_obj_set_width(waiting, 310); lv_obj_set_style_text_align(waiting, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(waiting, LV_ALIGN_TOP_MID, 0, 42);
        // 主界面只保留三键；原音量和静音通过长按「音乐」进入，不删现有功能。
        // Keep three primary controls; long-press Music reveals existing volume/mute.
        action(120, 190, 60, 60, LV_SYMBOL_PREV, NULL, HID_MEDIA_PREVIOUS);
        lv_obj_t *toggle = action(197, 182, 72, 72, LV_SYMBOL_PLAY " " LV_SYMBOL_PAUSE, NULL, HID_MEDIA_PLAY_PAUSE);
        g_play_symbol = lv_obj_get_child(toggle, 0);
        action(286, 190, 60, 60, LV_SYMBOL_NEXT, NULL, HID_MEDIA_NEXT);
        for (int i=0;i<3;i++) {
            lv_obj_set_style_radius(g_controls[i], LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(g_controls[i], 1, 0);
            lv_obj_set_style_border_color(g_controls[i], lv_color_hex(0x40464d), 0);
        }
        g_volume_panel = lv_obj_create(g_body); lv_obj_remove_style_all(g_volume_panel);
        lv_obj_set_pos(g_volume_panel, 54, 70); lv_obj_set_size(g_volume_panel, 358, 110);
        lv_obj_set_style_bg_color(g_volume_panel, lv_color_hex(0x111418), 0);
        lv_obj_set_style_bg_opa(g_volume_panel, LV_OPA_COVER, 0); lv_obj_set_style_radius(g_volume_panel, 24, 0);
        ui_obj_set_scrollable(g_volume_panel, false);
        const uint16_t uses[]={HID_MEDIA_VOLUME_DOWN,HID_MEDIA_MUTE,HID_MEDIA_VOLUME_UP};
        const str_id_t names[]={S_REMOTE_VOLUME_DOWN,S_REMOTE_MUTE,S_REMOTE_VOLUME_UP};
        for (int i=0;i<3;i++) {
            lv_obj_t *b=action(18+i*108,36,102,46,NULL,tr(names[i]),uses[i]);
            lv_obj_set_parent(b,g_volume_panel);
        }
        lv_obj_t *close=label(g_volume_panel, tr(S_REMOTE_HOLD_VOLUME), UI_FONT_M);
        lv_obj_set_style_text_color(close,lv_color_hex(0x929294),0); lv_obj_align(close,LV_ALIGN_TOP_MID,0,8);
        ui_obj_set_hidden(g_volume_panel,true);
        s_rotation_at=lv_tick_get(); update_media_view();
    }
    s_ready = !ble_hid_ready(s_mode); // 强制同步新一组控件的 enabled 状态。
    s_status = -1; update_connection();
    launcher_set_title(tr(s_mode == HID_MEDIA ? S_REMOTE_MEDIA : s_mode == HID_KEYBOARD ? S_REMOTE_SLIDES : S_REMOTE_TITLE));
}

static bool mouse_back(void) {
    if (!s_volume_open) return false;
    s_volume_open = false; ui_obj_set_hidden(g_volume_panel, true); return true;
}

static void update_connection(void) {
    bool ready = s_started && ble_hid_ready(s_mode);
    if (ready != s_ready) {
        if (!ready) clear_input();
        s_ready = ready;
        for (int i = 0; i < s_control_count; i++) {
            if (ready) lv_obj_remove_state(g_controls[i], LV_STATE_DISABLED);
            else lv_obj_add_state(g_controls[i], LV_STATE_DISABLED);
        }
    }
    int state = !s_started ? 0 : !ble_hid_connected() ? 1 : !ready ? 2 :
                esp_timer_get_time() < s_feedback_until ? 4 : 3;
    if (state != s_status) {
        s_status = state;
        const str_id_t messages[] = { S_BT_FAIL, S_MOUSE_PAIR, S_REMOTE_PREPARING, S_CONNECTED, S_REMOTE_BUSY };
        lv_label_set_text(g_status, tr(messages[state]));
        lv_obj_set_style_text_color(g_status, lv_color_hex(state == 3 ? COL_TXT : state == 4 ? COL_RED : COL_TXT2), 0);
    }
}

static void mouse_enter(lv_obj_t *parent) {
    g_parent = parent; s_mode = HID_MOUSE; s_visible = true;
    s_elapsed_us = s_timer_since = s_feedback_until = 0; s_timer_running = false;
    s_btns = 0; s_drag = false; s_ax = s_ay = s_wacc = 0;
    remote_media_set_visible(true);
    g_status = label(parent, tr(S_STARTING), UI_FONT_M);
    lv_obj_set_width(g_status, 324); lv_obj_set_style_text_align(g_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_status, LV_ALIGN_TOP_MID, 0, 88);
    const str_id_t names[] = { S_REMOTE_MOUSE, S_REMOTE_SLIDES, S_REMOTE_MEDIA };
    for (int i = 0; i < HID_REPORT_COUNT; i++) {
        g_modes[i] = button(parent, 120 + i * 78, 382, 70, 42);
        // unscii_16 英文字符也是 16px 宽;窄模式按钮使用现有比例字体及中文 fallback。
        lv_obj_center(label(g_modes[i], tr(names[i]), UI_FONT_SYM));
        lv_obj_add_event_cb(g_modes[i], mode_event, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        if (i == HID_MEDIA) lv_obj_add_event_cb(g_modes[i], volume_event, LV_EVENT_LONG_PRESSED, NULL);
    }
    s_started = ble_hid_start(); build_mode();
}

static void mouse_tick(void) {
    // 即使锁屏/快捷面板遮挡也排空释放报文,避免电脑把按键一直当成按住。
    ble_hid_tick();
    if (!s_visible || !g_status) return;
    update_connection();
    update_media_view();
    if (g_time) {
        int64_t elapsed = s_elapsed_us + (s_timer_running ? esp_timer_get_time() - s_timer_since : 0);
        unsigned seconds = (unsigned)(elapsed / 1000000);
        if (seconds != s_shown_seconds) {
            char text[16];
            if (seconds < 3600) snprintf(text, sizeof text, "%02u:%02u", seconds / 60, seconds % 60);
            else snprintf(text, sizeof text, "%u:%02u:%02u", seconds / 3600, seconds / 60 % 60, seconds % 60);
            lv_label_set_text(g_time, text); s_shown_seconds = seconds;
        }
    }
}

static void mouse_visibility(bool visible) {
    s_visible = visible;
    remote_media_set_visible(visible);
    if (!visible) clear_input();
}

static void mouse_exit(void) {
    clear_input(); ble_hid_stop();
    remote_media_set_visible(false); free_media_view();
    g_parent = g_body = g_status = g_pad = g_drag = g_time = NULL;
    s_started = s_ready = s_timer_running = false;
}

const app_t app_mouse = {
    .name = "mouse", .color = COL_TXT, .enter = mouse_enter, .tick = mouse_tick, .exit = mouse_exit,
    .back = mouse_back, .tick_period_ms = 20, .visibility = mouse_visibility, .tick_in_background = true,
};
