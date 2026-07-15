// 系统 app —— 亮度/音量滑块 + 表盘选择 + 两个开关 + 底部 about(徽标/版本/设备)。
// 可滚动单页:原来 about 是独立 app,合并进来省一个入口。改动实时应用,松手/点选写 NVS。
#include "app.h"
#include "settings.h"
#include "watchface.h"
#include "glyph.h"
#include "audio_out.h"
#include "esp_app_desc.h"
#include <stdio.h>

static lv_obj_t *g_br_val, *g_vol_val, *g_face_val;

static void br_changed(lv_event_t *e) {
    int v = lv_slider_get_value(lv_event_get_target_obj(e));
    settings_set_brightness((uint8_t)v);
    char b[8]; snprintf(b, sizeof b, "%d%%", v * 100 / 255);
    lv_label_set_text(g_br_val, b);
}
static void vol_changed(lv_event_t *e) {
    int v = lv_slider_get_value(lv_event_get_target_obj(e));
    settings_set_volume((uint8_t)v);
    audio_out_set_volume((uint8_t)v);                         // 实时应用到扬声器
    char b[8]; snprintf(b, sizeof b, "%d%%", v);
    lv_label_set_text(g_vol_val, b);
}
static void on_released(lv_event_t *e) { settings_save(); }   // 松手存 NVS
static void vol_blip(lv_event_t *e)   { audio_out_blip(); }   // 松手放一声,听当前音量

static void aod_changed(lv_event_t *e) {                       // always-on:开=低功耗长显 AOD,关=自动熄屏
    bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    settings_set_idle_mode(on ? IDLE_AOD : IDLE_OFF);
    settings_save();
}
static void silent_changed(lv_event_t *e) {                    // 静音:闹钟/提示音关闭
    bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    settings_set_silent(on ? 1 : 0);
    settings_save();
}

static void face_cb(lv_event_t *e) {                           // 表盘选择:循环 ±1,立即重建锁屏表盘 + 存 NVS
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    int n = watchface_count();
    int idx = (watchface_selected() + dir + n) % n;
    watchface_select(idx);                                     // 隐藏态重建,下次亮锁屏即新表盘
    settings_set_face((uint8_t)idx);
    settings_save();
    lv_label_set_text(g_face_val, watchface_name(idx));
}

static lv_obj_t *mk_face_btn(lv_obj_t *parent, const char *sym, int rx, int y, int dir) {
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 48, 40);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1c1c22), 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, rx, y);
    lv_obj_add_event_cb(btn, face_cb, LV_EVENT_CLICKED, (void *)(intptr_t)dir);
    lv_obj_t *l = lv_label_create(btn);
    lv_obj_set_style_text_font(l, UI_FONT_SYM, 0);
    lv_label_set_text(l, sym);
    lv_obj_center(l);
    return btn;
}

static lv_obj_t *mk_row(lv_obj_t *parent, const char *name, int y, int mn, int mx, int val,
                        lv_event_cb_t chg, lv_obj_t **vlabel) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, UI_FONT_L, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TXT), 0);
    lv_label_set_text(lbl, name);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 92, y);

    *vlabel = lv_label_create(parent);
    lv_obj_set_style_text_font(*vlabel, UI_FONT_M, 0);
    lv_obj_set_style_text_color(*vlabel, lv_color_hex(COL_TXT2), 0);
    lv_obj_align(*vlabel, LV_ALIGN_TOP_RIGHT, -92, y + 3);

    lv_obj_t *sl = lv_slider_create(parent);
    lv_obj_set_width(sl, 280);
    lv_slider_set_range(sl, mn, mx);
    lv_slider_set_value(sl, val, LV_ANIM_OFF);
    lv_obj_align(sl, LV_ALIGN_TOP_MID, 0, y + 34);
    lv_obj_remove_flag(sl, LV_OBJ_FLAG_GESTURE_BUBBLE);   // 横向拖动滑块不再冒泡成整屏手势 → 不误触返回
    lv_obj_add_event_cb(sl, chg, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(sl, on_released, LV_EVENT_RELEASED, NULL);
    return sl;
}

// 分区小标题(灰,左对齐):把设置分成 display / sound / system 三块,视觉更有层次
static lv_obj_t *mk_header(lv_obj_t *p, const char *t, int y) {
    lv_obj_t *h = lv_label_create(p);
    lv_obj_set_style_text_font(h, UI_FONT_M, 0);
    lv_obj_set_style_text_color(h, lv_color_hex(COL_TXT2), 0);
    lv_obj_set_style_text_letter_space(h, 3, 0);   // 拉开字距,更像"分区标签"
    lv_label_set_text(h, t);
    lv_obj_align(h, LV_ALIGN_TOP_LEFT, 100, y);
    return h;
}

static void settings_enter(lv_obj_t *parent) {
    audio_out_init();                 // 起扬声器(音量预览 + 之后的提示音);退出时释放

    // 可滚动单页,分区排布:display / sound / system(about)。往下滑看后两区
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, lv_pct(100), lv_pct(100));
    lv_obj_set_scroll_dir(page, LV_DIR_VER);                 // 只纵向滚(横向留给返回手势/滑块)
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(page, LV_OBJ_FLAG_EVENT_BUBBLE);         // 右滑返回照常冒泡到 app_screen

    char b[8];

    /* ================= DISPLAY ================= */
    mk_header(page, "display", 44);
    mk_row(page, "brightness", 76, SETTINGS_BRIGHT_MIN, 0xFF, settings_brightness(), br_changed, &g_br_val);
    snprintf(b, sizeof b, "%d%%", settings_brightness() * 100 / 255);
    lv_label_set_text(g_br_val, b);

    // face:锁屏表盘选择 ‹ 名字 ›(点击立即生效)
    lv_obj_t *fl = lv_label_create(page);
    lv_obj_set_style_text_font(fl, UI_FONT_L, 0);
    lv_obj_set_style_text_color(fl, lv_color_hex(COL_TXT), 0);
    lv_label_set_text(fl, "face");
    lv_obj_align(fl, LV_ALIGN_TOP_LEFT, 96, 172);
    mk_face_btn(page, LV_SYMBOL_LEFT,  -264, 164, -1);
    g_face_val = lv_label_create(page);
    lv_obj_set_style_text_font(g_face_val, UI_FONT_M, 0);
    lv_obj_set_style_text_color(g_face_val, lv_color_hex(COL_TXT2), 0);
    lv_obj_set_width(g_face_val, 108);
    lv_obj_set_style_text_align(g_face_val, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(g_face_val, watchface_name(watchface_selected()));
    lv_obj_align(g_face_val, LV_ALIGN_TOP_RIGHT, -148, 175);
    mk_face_btn(page, LV_SYMBOL_RIGHT, -92, 164, +1);

    // always-on:开=低功耗长显,关=自动熄屏
    lv_obj_t *al = lv_label_create(page);
    lv_obj_set_style_text_font(al, UI_FONT_L, 0);
    lv_obj_set_style_text_color(al, lv_color_hex(COL_TXT), 0);
    lv_label_set_text(al, "always-on");
    lv_obj_align(al, LV_ALIGN_TOP_LEFT, 96, 226);
    lv_obj_t *sw1 = lv_switch_create(page);
    lv_obj_align(sw1, LV_ALIGN_TOP_RIGHT, -96, 222);
    lv_obj_remove_flag(sw1, LV_OBJ_FLAG_GESTURE_BUBBLE);
    if (settings_idle_mode() == IDLE_AOD) lv_obj_add_state(sw1, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw1, aod_changed, LV_EVENT_VALUE_CHANGED, NULL);

    glyph_line(page, 116, 274, 350, 274, 12, 2, 0x2a2a30);  // 分区分隔线(暗)

    /* ================= SOUND ================= */
    mk_header(page, "sound", 296);
    lv_obj_t *vsl = mk_row(page, "volume", 328, 0, 100, settings_volume(), vol_changed, &g_vol_val);
    lv_obj_add_event_cb(vsl, vol_blip, LV_EVENT_RELEASED, NULL);   // 松手试听当前音量
    snprintf(b, sizeof b, "%d%%", settings_volume());
    lv_label_set_text(g_vol_val, b);

    // silent:静音(闹钟/提示音关闭)
    lv_obj_t *sl2 = lv_label_create(page);
    lv_obj_set_style_text_font(sl2, UI_FONT_L, 0);
    lv_obj_set_style_text_color(sl2, lv_color_hex(COL_TXT), 0);
    lv_label_set_text(sl2, "silent");
    lv_obj_align(sl2, LV_ALIGN_TOP_LEFT, 96, 424);
    lv_obj_t *sw2 = lv_switch_create(page);
    lv_obj_align(sw2, LV_ALIGN_TOP_RIGHT, -96, 420);
    lv_obj_remove_flag(sw2, LV_OBJ_FLAG_GESTURE_BUBBLE);
    if (settings_silent()) lv_obj_add_state(sw2, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw2, silent_changed, LV_EVENT_VALUE_CHANGED, NULL);

    glyph_line(page, 116, 472, 350, 472, 12, 2, 0x2a2a30);  // 分区分隔线(暗)

    /* ================= SYSTEM (about) ================= */
    mk_header(page, "system", 494);
    int ly = 562;                                           // 同心点描环徽标(品牌:圆)+ 红核
    glyph_circle(page, 233, ly, 42, 13, 3, COL_TXT);
    glyph_circle(page, 233, ly, 27, 12, 3, COL_TXT);
    glyph_dot(page, 233, ly, 6, COL_RED);

    lv_obj_t *row = lv_obj_create(page);                    // 字标 soRound(白) + OS(红)
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 620);
    lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_t *n1 = lv_label_create(row);
    lv_obj_set_style_text_font(n1, UI_FONT_SYM, 0);
    lv_obj_set_style_text_color(n1, lv_color_hex(COL_TXT), 0);
    lv_label_set_text(n1, "soRound");
    lv_obj_t *n2 = lv_label_create(row);
    lv_obj_set_style_text_font(n2, UI_FONT_SYM, 0);
    lv_obj_set_style_text_color(n2, lv_color_hex(COL_RED), 0);
    lv_label_set_text(n2, "OS");

    // 版本号取真实固件版本(esp_app_desc,随 OTA 自动变),不再写死
    const esp_app_desc_t *ad = esp_app_get_description();
    char vb[40]; snprintf(vb, sizeof vb, "version %s", ad->version);
    lv_obj_t *ver = lv_label_create(page);
    lv_obj_set_style_text_font(ver, UI_FONT_M, 0);
    lv_obj_set_style_text_color(ver, lv_color_hex(COL_TXT2), 0);
    lv_obj_set_style_text_align(ver, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(ver, vb);
    lv_obj_align(ver, LV_ALIGN_TOP_MID, 0, 666);

    lv_obj_t *dev = lv_label_create(page);
    lv_obj_set_style_text_font(dev, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(dev, lv_color_hex(COL_TXT2), 0);
    lv_obj_set_style_text_align(dev, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(dev, "ESP32-S3  /  466 round");
    lv_obj_align(dev, LV_ALIGN_TOP_MID, 0, 694);
}

static void settings_exit(void) { audio_out_deinit(); g_br_val = g_vol_val = g_face_val = NULL; }

const app_t app_settings = { "settings", COL_TXT, settings_enter, NULL, settings_exit };
