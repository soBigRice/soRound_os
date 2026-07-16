// 系统 app —— 二级抽屉式(drill-down),适配小圆屏:
//   一级 = 分区菜单(display / sound / system),每行显示当前值 + ›,点进二级;
//   二级 = 单控件全屏页(放大居中,好操作):亮度/音量大读数+滑块、表盘列表、开关+说明、about。
//   返回:右滑或顶部 ‹ —— 框架先调 settings_back()(在二级则退回一级),一级再右滑才退出 app。
#include "app.h"
#include "settings.h"
#include "watchface.h"
#include "glyph.h"
#include "audio_out.h"
#include "esp_app_desc.h"
#include <stdio.h>

#define CARD_W   306
#define ROW_H    58
#define CARD_BG  0x16161c
#define DIV_COL  0x2a2a30
#define BTN_BG   0x26262c
#define UI_FONT_XL &lv_font_montserrat_40   // 二级页大读数

enum { IT_BRIGHT, IT_FACE, IT_AOD, IT_VOL, IT_SILENT, IT_ABOUT };

static lv_obj_t *s_menu, *s_detail, *s_dval;   // 一级菜单 / 二级容器 / 二级大读数标签
static int       s_cur = -1;                   // 当前打开的二级项,-1=在一级菜单

static void build_menu(void);

/* ===================== 应用/回调(实时生效,松手或点选写 NVS) ===================== */
static void on_released(lv_event_t *e) { (void)e; settings_save(); }

static void br_changed(lv_event_t *e) {
    int v = lv_slider_get_value(lv_event_get_target_obj(e));
    settings_set_brightness((uint8_t)v);
    if (s_dval) { char b[8]; snprintf(b, sizeof b, "%d%%", v * 100 / 255); lv_label_set_text(s_dval, b); }
}
static void vol_changed(lv_event_t *e) {
    int v = lv_slider_get_value(lv_event_get_target_obj(e));
    settings_set_volume((uint8_t)v);
    audio_out_set_volume((uint8_t)v);
    if (s_dval) { char b[8]; snprintf(b, sizeof b, "%d%%", v); lv_label_set_text(s_dval, b); }
}
static void vol_blip(lv_event_t *e) { (void)e; audio_out_blip(); }

static void aod_changed(lv_event_t *e) {
    bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    settings_set_idle_mode(on ? IDLE_AOD : IDLE_OFF);
    settings_save();
}
static void silent_changed(lv_event_t *e) {
    bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    settings_set_silent(on ? 1 : 0);
    settings_save();
}

/* ===================== 一级:分区卡片菜单 ===================== */
static void mk_section(lv_obj_t *page, const char *t) {
    lv_obj_t *h = lv_label_create(page);
    lv_obj_set_width(h, CARD_W);
    lv_obj_set_style_pad_left(h, 22, 0);
    lv_obj_set_style_pad_top(h, 14, 0);
    lv_obj_set_style_pad_bottom(h, 8, 0);
    lv_obj_set_style_text_font(h, UI_FONT_M, 0);
    lv_obj_set_style_text_color(h, lv_color_hex(COL_TXT2), 0);
    lv_obj_set_style_text_letter_space(h, 4, 0);
    lv_label_set_text(h, t);
}
static lv_obj_t *mk_card(lv_obj_t *page) {
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
static void mk_divider(lv_obj_t *card) {
    lv_obj_t *d = lv_obj_create(card);
    lv_obj_remove_style_all(d);
    lv_obj_set_width(d, lv_pct(100));
    lv_obj_set_height(d, 1);
    lv_obj_set_style_bg_color(d, lv_color_hex(DIV_COL), 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
}

static void open_detail(int id);
static void item_click(lv_event_t *e) { open_detail((int)(intptr_t)lv_event_get_user_data(e)); }

// 菜单行:标题左,(值 + ›)右;整行可点 → 进二级
static void menu_item(lv_obj_t *card, const char *title, const char *value, int id) {
    lv_obj_t *r = lv_obj_create(card);
    lv_obj_remove_style_all(r);
    lv_obj_set_width(r, lv_pct(100));
    lv_obj_set_height(r, ROW_H);
    lv_obj_set_style_pad_left(r, 20, 0);
    lv_obj_set_style_pad_right(r, 16, 0);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(r, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_bg_color(r, lv_color_hex(0x24242c), LV_STATE_PRESSED);   // 按下反馈
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_add_event_cb(r, item_click, LV_EVENT_CLICKED, (void *)(intptr_t)id);

    lv_obj_t *t = lv_label_create(r);
    lv_obj_set_style_text_font(t, UI_FONT_L, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(COL_TXT), 0);
    lv_label_set_text(t, title);

    lv_obj_t *rt = lv_obj_create(r);                    // 右侧:值 + 箭头
    lv_obj_remove_style_all(rt);
    lv_obj_set_size(rt, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(rt, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rt, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(rt, 8, 0);
    lv_obj_add_flag(rt, LV_OBJ_FLAG_EVENT_BUBBLE);
    if (value && value[0]) {
        lv_obj_t *v = lv_label_create(rt);
        lv_obj_set_style_text_font(v, UI_FONT_M, 0);
        lv_obj_set_style_text_color(v, lv_color_hex(COL_TXT2), 0);
        lv_label_set_text(v, value);
    }
    lv_obj_t *ch = lv_label_create(rt);
    lv_obj_set_style_text_font(ch, UI_FONT_SYM, 0);
    lv_obj_set_style_text_color(ch, lv_color_hex(0x55555c), 0);
    lv_label_set_text(ch, LV_SYMBOL_RIGHT);
}

static void build_menu(void) {
    lv_obj_clean(s_menu);
    char b[16];

    mk_section(s_menu, "display");
    lv_obj_t *cd = mk_card(s_menu);
    snprintf(b, sizeof b, "%d%%", settings_brightness() * 100 / 255);
    menu_item(cd, "brightness", b, IT_BRIGHT);
    mk_divider(cd);
    menu_item(cd, "face", watchface_name(watchface_selected()), IT_FACE);
    mk_divider(cd);
    menu_item(cd, "always-on", settings_idle_mode() == IDLE_AOD ? "on" : "off", IT_AOD);

    mk_section(s_menu, "sound");
    lv_obj_t *cs = mk_card(s_menu);
    snprintf(b, sizeof b, "%d%%", settings_volume());
    menu_item(cs, "volume", b, IT_VOL);
    mk_divider(cs);
    menu_item(cs, "silent", settings_silent() ? "on" : "off", IT_SILENT);

    mk_section(s_menu, "system");
    lv_obj_t *cy = mk_card(s_menu);
    menu_item(cy, "about", "", IT_ABOUT);
}

/* ===================== 二级:单控件全屏页 ===================== */
static void detail_title(const char *t) {
    lv_obj_t *h = lv_label_create(s_detail);
    lv_obj_set_style_text_font(h, UI_FONT_M, 0);
    lv_obj_set_style_text_color(h, lv_color_hex(COL_TXT2), 0);
    lv_obj_set_style_text_letter_space(h, 4, 0);
    lv_label_set_text(h, t);
    lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 64);
}

// 亮度/音量:大 % 读数 + 整宽滑块,圆屏居中
static void build_slider_detail(const char *title, int mn, int mx, int val,
                                lv_event_cb_t chg, bool is_vol) {
    detail_title(title);
    s_dval = lv_label_create(s_detail);
    lv_obj_set_style_text_font(s_dval, UI_FONT_XL, 0);
    lv_obj_set_style_text_color(s_dval, lv_color_hex(COL_TXT), 0);
    lv_obj_align(s_dval, LV_ALIGN_CENTER, 0, -34);
    char b[8]; snprintf(b, sizeof b, "%d%%", is_vol ? val : val * 100 / 255);
    lv_label_set_text(s_dval, b);

    lv_obj_t *sl = lv_slider_create(s_detail);
    lv_obj_set_width(sl, 300);
    lv_slider_set_range(sl, mn, mx);
    lv_slider_set_value(sl, val, LV_ANIM_OFF);
    lv_obj_align(sl, LV_ALIGN_CENTER, 0, 60);
    lv_obj_remove_flag(sl, LV_OBJ_FLAG_GESTURE_BUBBLE);   // 横向拖动不误触返回
    lv_obj_add_event_cb(sl, chg, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(sl, on_released, LV_EVENT_RELEASED, NULL);
    if (is_vol) lv_obj_add_event_cb(sl, vol_blip, LV_EVENT_RELEASED, NULL);
}

// 开关页:大开关 + 一行说明,点开关切换
static void build_switch_detail(const char *title, const char *desc, bool on, lv_event_cb_t cb) {
    detail_title(title);
    lv_obj_t *sw = lv_switch_create(s_detail);
    lv_obj_set_size(sw, 92, 46);                          // 放大好点
    lv_obj_align(sw, LV_ALIGN_CENTER, 0, -20);
    lv_obj_remove_flag(sw, LV_OBJ_FLAG_GESTURE_BUBBLE);
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *d = lv_label_create(s_detail);
    lv_obj_set_style_text_font(d, UI_FONT_M, 0);
    lv_obj_set_style_text_color(d, lv_color_hex(COL_TXT2), 0);
    lv_obj_set_style_text_align(d, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(d, 300);
    lv_label_set_text(d, desc);
    lv_obj_align(d, LV_ALIGN_CENTER, 0, 56);
}

static void face_pick(lv_event_t *e) {                   // 表盘列表点选:立即应用 + 原地刷新高亮
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    watchface_select(idx);
    settings_set_face((uint8_t)idx);
    settings_save();
    // 原地改样式,不重建(在自身点击回调里 clean 父容器 = 删正在执行回调的对象,LVGL 崩溃隐患)
    lv_obj_t *list = lv_obj_get_parent(lv_event_get_target_obj(e));
    int n = (int)lv_obj_get_child_count(list);
    for (int i = 0; i < n; i++) {
        lv_obj_t *it = lv_obj_get_child(list, i);
        lv_obj_set_style_bg_color(it, lv_color_hex(i == idx ? 0x2a1216 : CARD_BG), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(it, 0), lv_color_hex(i == idx ? COL_RED : COL_TXT), 0);
    }
}
// 表盘页:纵向列表,当前项红色高亮,点即选
static void build_face_detail(void) {
    detail_title("face");
    lv_obj_t *list = lv_obj_create(s_detail);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 300, 320);
    lv_obj_align(list, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(list, LV_OBJ_FLAG_EVENT_BUBBLE);

    int cur = watchface_selected(), n = watchface_count();
    for (int i = 0; i < n; i++) {
        lv_obj_t *it = lv_obj_create(list);
        lv_obj_remove_style_all(it);
        lv_obj_set_size(it, 240, 50);
        lv_obj_set_style_radius(it, 14, 0);
        lv_obj_set_style_bg_color(it, lv_color_hex(i == cur ? 0x2a1216 : CARD_BG), 0);
        lv_obj_set_style_bg_opa(it, LV_OPA_COVER, 0);
        lv_obj_add_flag(it, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(it, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_event_cb(it, face_pick, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *l = lv_label_create(it);
        lv_obj_set_style_text_font(l, UI_FONT_L, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(i == cur ? COL_RED : COL_TXT), 0);
        lv_label_set_text(l, watchface_name(i));
        lv_obj_center(l);
    }
}

// about 页:徽标 + 字标 + 真实版本 + 设备
static void build_about_detail(void) {
    int cx = 233, ly = 150;
    glyph_circle(s_detail, cx, ly, 44, 13, 3, COL_TXT);
    glyph_circle(s_detail, cx, ly, 28, 12, 3, COL_TXT);
    glyph_dot(s_detail, cx, ly, 6, COL_RED);

    lv_obj_t *row = lv_obj_create(s_detail);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 232);
    lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_t *n1 = lv_label_create(row);
    lv_obj_set_style_text_font(n1, UI_FONT_SYM, 0);
    lv_obj_set_style_text_color(n1, lv_color_hex(COL_TXT), 0);
    lv_label_set_text(n1, "soRound");
    lv_obj_t *n2 = lv_label_create(row);
    lv_obj_set_style_text_font(n2, UI_FONT_SYM, 0);
    lv_obj_set_style_text_color(n2, lv_color_hex(COL_RED), 0);
    lv_label_set_text(n2, "OS");

    const esp_app_desc_t *ad = esp_app_get_description();
    char vb[40]; snprintf(vb, sizeof vb, "version %s", ad->version);
    lv_obj_t *ver = lv_label_create(s_detail);
    lv_obj_set_style_text_font(ver, UI_FONT_M, 0);
    lv_obj_set_style_text_color(ver, lv_color_hex(COL_TXT2), 0);
    lv_label_set_text(ver, vb);
    lv_obj_align(ver, LV_ALIGN_TOP_MID, 0, 278);

    lv_obj_t *dev = lv_label_create(s_detail);
    lv_obj_set_style_text_font(dev, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(dev, lv_color_hex(COL_TXT2), 0);
    lv_label_set_text(dev, "ESP32-S3  /  466 round");
    lv_obj_align(dev, LV_ALIGN_TOP_MID, 0, 306);
}

static void open_detail(int id) {
    lv_obj_clean(s_detail);
    s_dval = NULL;
    switch (id) {
        case IT_BRIGHT: build_slider_detail("brightness", SETTINGS_BRIGHT_MIN, 0xFF, settings_brightness(), br_changed, false); break;
        case IT_VOL:    build_slider_detail("volume", 0, 100, settings_volume(), vol_changed, true); break;
        case IT_AOD:    build_switch_detail("always-on", "screen stays dimmed when idle\ninstead of turning off", settings_idle_mode() == IDLE_AOD, aod_changed); break;
        case IT_SILENT: build_switch_detail("silent", "mute alarms and beeps", settings_silent(), silent_changed); break;
        case IT_FACE:   build_face_detail(); break;
        case IT_ABOUT:  build_about_detail(); break;
    }
    s_cur = id;
    lv_obj_add_flag(s_menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_detail, LV_OBJ_FLAG_HIDDEN);
    launcher_set_title(id == IT_ABOUT ? "about" : "settings");
}

// 框架返回回调:在二级 → 退回一级菜单(消费返回);在一级 → 返 false 让框架退出 app
static bool settings_back(void) {
    if (s_cur < 0) return false;
    s_cur = -1;
    lv_obj_add_flag(s_detail, LV_OBJ_FLAG_HIDDEN);
    build_menu();                                        // 重建菜单:刷新各行当前值
    lv_obj_remove_flag(s_menu, LV_OBJ_FLAG_HIDDEN);
    launcher_set_title("settings");
    return true;
}

/* ===================== 生命周期 ===================== */
static void settings_enter(lv_obj_t *parent) {
    audio_out_init();                 // 音量试听/提示音;退出释放

    s_menu = lv_obj_create(parent);   // 一级:纵向 flex 分区卡片列表
    lv_obj_remove_style_all(s_menu);
    lv_obj_set_size(s_menu, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(s_menu, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_menu, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(s_menu, 62, 0);
    lv_obj_set_style_pad_bottom(s_menu, 70, 0);
    lv_obj_set_style_pad_row(s_menu, 4, 0);
    lv_obj_set_scroll_dir(s_menu, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_menu, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(s_menu, LV_OBJ_FLAG_EVENT_BUBBLE);

    s_detail = lv_obj_create(parent);  // 二级:单控件全屏,默认隐藏
    lv_obj_remove_style_all(s_detail);
    lv_obj_set_size(s_detail, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(s_detail, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_detail, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(s_detail, LV_OBJ_FLAG_HIDDEN);

    s_cur = -1;
    s_dval = NULL;
    build_menu();
}

static void settings_exit(void) {
    audio_out_deinit();
    s_menu = s_detail = s_dval = NULL;
    s_cur = -1;
}

const app_t app_settings = { "settings", COL_TXT, settings_enter, NULL, settings_exit, settings_back };
