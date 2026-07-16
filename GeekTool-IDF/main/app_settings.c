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

/* ===== grouped-list 组件:分区标题 + 卡片 + 统一行,用 flex 强制一致(不再绝对坐标)===== */
#define CARD_W   324
#define ROW_H    58
#define CARD_BG  0x16161c
#define DIV_COL  0x2a2a30
#define BTN_BG   0x26262c

static void mk_section(lv_obj_t *page, const char *t) {          // 分区标题(灰、拉字距、左对齐于卡片列)
    lv_obj_t *h = lv_label_create(page);
    lv_obj_set_width(h, CARD_W);
    lv_obj_set_style_pad_left(h, 22, 0);
    lv_obj_set_style_pad_top(h, 16, 0);
    lv_obj_set_style_pad_bottom(h, 8, 0);
    lv_obj_set_style_text_font(h, UI_FONT_M, 0);
    lv_obj_set_style_text_color(h, lv_color_hex(COL_TXT2), 0);
    lv_obj_set_style_text_letter_space(h, 4, 0);
    lv_label_set_text(h, t);
}

static lv_obj_t *mk_card(lv_obj_t *page) {                       // 圆角深色卡片,纵向 flex 容纳若干行
    lv_obj_t *c = lv_obj_create(page);
    lv_obj_remove_style_all(c);
    lv_obj_set_width(c, CARD_W);
    lv_obj_set_height(c, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(c, lv_color_hex(CARD_BG), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 20, 0);
    lv_obj_set_style_pad_top(c, 2, 0);
    lv_obj_set_style_pad_bottom(c, 2, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_EVENT_BUBBLE);
    return c;
}

static void mk_divider(lv_obj_t *card) {                         // 行间发丝分隔线
    lv_obj_t *d = lv_obj_create(card);
    lv_obj_remove_style_all(d);
    lv_obj_set_width(d, lv_pct(100));
    lv_obj_set_height(d, 1);
    lv_obj_set_style_bg_color(d, lv_color_hex(DIV_COL), 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
}

// 统一行:标题左、控件右(SPACE_BETWEEN)。返回行,调用方把右侧控件塞进去即自动右对齐
static lv_obj_t *mk_row(lv_obj_t *card, const char *title, int h) {
    lv_obj_t *r = lv_obj_create(card);
    lv_obj_remove_style_all(r);
    lv_obj_set_width(r, lv_pct(100));
    lv_obj_set_height(r, h);
    lv_obj_set_style_pad_left(r, 20, 0);
    lv_obj_set_style_pad_right(r, 18, 0);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(r, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_t *t = lv_label_create(r);
    lv_obj_set_style_text_font(t, UI_FONT_L, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(COL_TXT), 0);
    lv_label_set_text(t, title);
    return r;
}

// 滑块行:上排标题+数值,下排整宽滑块(同在一行卡内,视觉归组)
static lv_obj_t *mk_slider_row(lv_obj_t *card, const char *title, int mn, int mx, int val,
                               lv_event_cb_t chg, lv_obj_t **vlabel) {
    lv_obj_t *r = lv_obj_create(card);
    lv_obj_remove_style_all(r);
    lv_obj_set_width(r, lv_pct(100));
    lv_obj_set_height(r, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_left(r, 20, 0);
    lv_obj_set_style_pad_right(r, 20, 0);
    lv_obj_set_style_pad_top(r, 14, 0);
    lv_obj_set_style_pad_bottom(r, 16, 0);
    lv_obj_set_style_pad_row(r, 12, 0);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(r, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *top = lv_obj_create(r);
    lv_obj_remove_style_all(top);
    lv_obj_set_width(top, lv_pct(100));
    lv_obj_set_height(top, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(top, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(top, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_t *t = lv_label_create(top);
    lv_obj_set_style_text_font(t, UI_FONT_L, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(COL_TXT), 0);
    lv_label_set_text(t, title);
    *vlabel = lv_label_create(top);
    lv_obj_set_style_text_font(*vlabel, UI_FONT_M, 0);
    lv_obj_set_style_text_color(*vlabel, lv_color_hex(COL_TXT2), 0);

    lv_obj_t *sl = lv_slider_create(r);
    lv_obj_set_width(sl, lv_pct(100));
    lv_slider_set_range(sl, mn, mx);
    lv_slider_set_value(sl, val, LV_ANIM_OFF);
    lv_obj_remove_flag(sl, LV_OBJ_FLAG_GESTURE_BUBBLE);   // 横向拖动不冒泡成整屏手势 → 不误触返回
    lv_obj_add_event_cb(sl, chg, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(sl, on_released, LV_EVENT_RELEASED, NULL);
    return sl;
}

// 表盘选择控件(‹ 名字 ›)塞进某行右侧
static void mk_face_ctl(lv_obj_t *row) {
    lv_obj_t *box = lv_obj_create(row);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(box, 8, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(box, LV_OBJ_FLAG_EVENT_BUBBLE);

    for (int i = 0; i < 2; i++) {
        int dir = i == 0 ? -1 : +1;
        const char *sym = i == 0 ? LV_SYMBOL_LEFT : LV_SYMBOL_RIGHT;
        if (i == 1) {                                    // 值标签夹在两钮之间
            g_face_val = lv_label_create(box);
            lv_obj_set_style_text_font(g_face_val, UI_FONT_M, 0);
            lv_obj_set_style_text_color(g_face_val, lv_color_hex(COL_TXT), 0);
            lv_obj_set_width(g_face_val, 66);
            lv_obj_set_style_text_align(g_face_val, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_text(g_face_val, watchface_name(watchface_selected()));
        }
        lv_obj_t *btn = lv_button_create(box);
        lv_obj_set_size(btn, 38, 34);
        lv_obj_set_style_bg_color(btn, lv_color_hex(BTN_BG), 0);
        lv_obj_set_style_radius(btn, 9, 0);
        lv_obj_add_event_cb(btn, face_cb, LV_EVENT_CLICKED, (void *)(intptr_t)dir);
        lv_obj_t *l = lv_label_create(btn);
        lv_obj_set_style_text_font(l, UI_FONT_SYM, 0);
        lv_label_set_text(l, sym);
        lv_obj_center(l);
    }
}

// 开关塞进某行右侧(统一处理 gesture / 初值 / 回调)
static void mk_switch(lv_obj_t *row, bool on, lv_event_cb_t cb) {
    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_remove_flag(sw, LV_OBJ_FLAG_GESTURE_BUBBLE);
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
}

static void settings_enter(lv_obj_t *parent) {
    audio_out_init();                 // 起扬声器(音量预览 + 之后的提示音);退出时释放

    // 纵向 flex 单页:分区标题 + 卡片,统一间距;横向居中让卡片/标题左缘对齐
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(page, 62, 0);                   // 让首个标题落在圆屏可视区(避开顶部标题栏)
    lv_obj_set_style_pad_bottom(page, 70, 0);
    lv_obj_set_style_pad_row(page, 6, 0);
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(page, LV_OBJ_FLAG_EVENT_BUBBLE);         // 右滑返回照常冒泡

    char b[8];

    /* ---- DISPLAY ---- */
    mk_section(page, "display");
    lv_obj_t *cd = mk_card(page);
    mk_slider_row(cd, "brightness", SETTINGS_BRIGHT_MIN, 0xFF, settings_brightness(), br_changed, &g_br_val);
    snprintf(b, sizeof b, "%d%%", settings_brightness() * 100 / 255);
    lv_label_set_text(g_br_val, b);
    mk_divider(cd);
    mk_face_ctl(mk_row(cd, "face", ROW_H));
    mk_divider(cd);
    mk_switch(mk_row(cd, "always-on", ROW_H), settings_idle_mode() == IDLE_AOD, aod_changed);

    /* ---- SOUND ---- */
    mk_section(page, "sound");
    lv_obj_t *cs = mk_card(page);
    lv_obj_t *vsl = mk_slider_row(cs, "volume", 0, 100, settings_volume(), vol_changed, &g_vol_val);
    lv_obj_add_event_cb(vsl, vol_blip, LV_EVENT_RELEASED, NULL);   // 松手试听
    snprintf(b, sizeof b, "%d%%", settings_volume());
    lv_label_set_text(g_vol_val, b);
    mk_divider(cs);
    mk_switch(mk_row(cs, "silent", ROW_H), settings_silent(), silent_changed);

    /* ---- SYSTEM (about) ---- */
    mk_section(page, "system");
    lv_obj_t *ab = lv_obj_create(page);                     // 信息块:非 flex,内部绝对摆徽标/文字
    lv_obj_remove_style_all(ab);
    lv_obj_set_size(ab, CARD_W, 208);
    lv_obj_remove_flag(ab, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ab, LV_OBJ_FLAG_EVENT_BUBBLE);

    int cx = CARD_W / 2, ly = 52;                           // 同心点描环徽标 + 红核
    glyph_circle(ab, cx, ly, 40, 13, 3, COL_TXT);
    glyph_circle(ab, cx, ly, 26, 12, 3, COL_TXT);
    glyph_dot(ab, cx, ly, 6, COL_RED);

    lv_obj_t *row = lv_obj_create(ab);                      // 字标 soRound(白)+ OS(红)
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 108);
    lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_t *n1 = lv_label_create(row);
    lv_obj_set_style_text_font(n1, UI_FONT_SYM, 0);
    lv_obj_set_style_text_color(n1, lv_color_hex(COL_TXT), 0);
    lv_label_set_text(n1, "soRound");
    lv_obj_t *n2 = lv_label_create(row);
    lv_obj_set_style_text_font(n2, UI_FONT_SYM, 0);
    lv_obj_set_style_text_color(n2, lv_color_hex(COL_RED), 0);
    lv_label_set_text(n2, "OS");

    const esp_app_desc_t *ad = esp_app_get_description();   // 真实固件版本(随 OTA 变)
    char vb[40]; snprintf(vb, sizeof vb, "version %s", ad->version);
    lv_obj_t *ver = lv_label_create(ab);
    lv_obj_set_style_text_font(ver, UI_FONT_M, 0);
    lv_obj_set_style_text_color(ver, lv_color_hex(COL_TXT2), 0);
    lv_label_set_text(ver, vb);
    lv_obj_align(ver, LV_ALIGN_TOP_MID, 0, 150);

    lv_obj_t *dev = lv_label_create(ab);
    lv_obj_set_style_text_font(dev, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(dev, lv_color_hex(COL_TXT2), 0);
    lv_label_set_text(dev, "ESP32-S3  /  466 round");
    lv_obj_align(dev, LV_ALIGN_TOP_MID, 0, 178);
}

static void settings_exit(void) { audio_out_deinit(); g_br_val = g_vol_val = g_face_val = NULL; }

const app_t app_settings = { "settings", COL_TXT, settings_enter, NULL, settings_exit };
