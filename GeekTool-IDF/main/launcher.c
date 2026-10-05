// 启动器(小面积运动设计)+ App 框架 + 导航 —— LVGL 9
// 静止黑底,居中一个大图标;切换只动中心一小块(短滑+淡入淡出,不整屏滑)→ 从设计上避开撕裂。
#include "app.h"
#include "power.h"
#include "lock.h"
#include "buttons.h"
#include "glyph.h"
#include "launcher_icons.h"
#include "tools_ui.h"
#include "weather_ui.h"
#include "weather_location_ui.h"
#include "quickpanel.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_pm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

#define ICON 196
#define SWAP_MS    220    // 切换动画总时长(ms);半程滑出、半程滑入
#define SWAP_SLIDE 56     // 中心块滑动幅度(px,越小越不易撕裂)

// 注册表
const app_t *const APPS[] = { &app_wifi, &app_i2c, &app_sys, &app_weather, &app_calendar, &app_countdown, &app_stopwatch, &app_settings, &app_ota, &app_audio, &app_level, &app_maze, &app_fluid, &app_dice, &app_mouse, &app_twin, &app_answers, &app_zodiac, &app_merit };
const int APP_COUNT = sizeof(APPS) / sizeof(APPS[0]);

static lv_obj_t *launcher_screen, *app_screen;
static lv_obj_t *g_icon, *g_iconart, *g_name, *g_title, *g_back, *g_batt, *g_bolt;
static lv_obj_t *g_back_label, *g_backdots;
static lv_obj_t *g_nextart;
static const app_t *cur_app;
static int cur, pending_app;
static pwr_state_t s_last_pwr = PWR_UNKNOWN;
static uint32_t s_last_batt_color = UINT32_MAX;
static bool s_app_visible;
static uint32_t s_tick_at;
static void battery_visibility(bool covered);
static bool battery_covered(void) {
    // OTA 按页面约定只显示一个点阵圆圈;仅在该 App 收起实线电量层。
    return lock_is_locked() || cur_app == &app_ota;
}
static void header_app_style(void) {
    // OTA 的贴边点阵环需要透明点阵返回键;其他 App 保持原有圆形按钮。
    bool ota = cur_app == &app_ota;
    bool weather = cur_app == &app_weather;
    bool controls = cur_app == &app_settings || cur_app == &app_wifi;
    bool book = cur_app == &app_answers || cur_app == &app_zodiac || cur_app == &app_merit;
    bool compact = weather || controls || book;
    bool tools = cur_app == &app_audio || cur_app == &app_level;
    // 天气页沿用系统返回/电量语义,仅局部匹配已确认的 AMOLED 版式。
    // 退出后恢复既有尺寸、字体和电量环,不把天气配色扩散到其他 App。
    lv_obj_set_style_text_font(g_title, compact ? &font_location_24 : UI_FONT_L, 0);
    lv_obj_set_style_text_letter_space(g_title, 0, 0);
    lv_obj_set_style_text_color(g_title, lv_color_hex(COL_TXT), 0);
    lv_obj_set_width(g_title,compact?166:LV_SIZE_CONTENT);
    lv_label_set_long_mode(g_title,LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(g_title,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_align(g_title, LV_ALIGN_TOP_MID, 0, compact ? 52 : 46);
    lv_obj_set_size(g_back, (controls || book) ? 44 : (compact ? 40 : 48), (controls || book) ? 44 : (compact ? 40 : 48));
    lv_obj_align(g_back, LV_ALIGN_TOP_MID, (controls || book) ? -110 : -100, compact ? 52 : 40);
    lv_obj_set_style_bg_color(g_back, lv_color_hex(weather ? 0x22272a : 0x16161a), 0);
    lv_obj_set_style_arc_width(g_batt, weather ? 6 : 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(g_batt, weather ? 6 : 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(g_batt, lv_color_hex(weather ? 0x22272a : (tools ? TOOLS_FAINT : 0x15151a)), LV_PART_MAIN);
    if (s_last_batt_color == COL_TXT || s_last_batt_color == TOOLS_WHITE) {
        s_last_batt_color = tools ? TOOLS_WHITE : COL_TXT;
        lv_obj_set_style_arc_color(g_batt,lv_color_hex(s_last_batt_color),LV_PART_INDICATOR);
    }
    lv_obj_set_style_bg_opa(g_back, ota ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_back, (ota || weather) ? 0 : 1, 0);
    lv_obj_set_style_border_color(g_back, lv_color_hex(COL_TXT2), 0);
    lv_obj_set_style_border_opa(g_back, LV_OPA_50, 0);
    lv_label_set_text(g_back_label, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(g_back_label, lv_color_hex(COL_TXT), 0);
    if (weather) lv_obj_set_style_text_font(g_back_label, &lv_font_montserrat_20, 0);
    else lv_obj_remove_local_style_prop(g_back_label, LV_STYLE_TEXT_FONT, 0);
    lv_obj_center(g_back_label);
    if (tools) {
        tools_header(g_title,g_back,g_back_label);
    }
    if (ota) {
        lv_obj_add_flag(g_back_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(g_backdots, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(g_back_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_backdots, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---- Native icon widget; the registry order is unchanged. ---- */
_Static_assert(sizeof(APPS)/sizeof(APPS[0])==LAUNCHER_ICON_COUNT,"Every app needs a launcher icon");
static void draw_icon(int i) {
    launcher_icon_set(g_iconart,(launcher_icon_t)i);
}

/* ---- 切换当前居中 app:换几何图标 + 名字 ---- */
static void apply_app(int i) {
    cur = i;
    draw_icon(i);
    lv_label_set_text(g_name, tr_app_name(APPS[i]->name));
}

/* ---- 小面积切换动画:中心图标+名字 半程滑出淡出 → 中点换内容 → 反向滑入淡入。
       黑底全程不动,只重绘中心一小块,从设计上避开整屏滑的撕裂。 ---- */
static int  swap_exit_x;     // 本次滑出的半程目标 x(正负取决于左右滑)
static int queued_dir;
static void nav(int dir);
static bool swapped;         // 本次动画是否已在中点换过内容

static void swap_exec(void *var, int32_t v) {        // v: 0..256
    int32_t  x;
    lv_opa_t opa;
    if (v < 128) {                                   // 前半:旧内容滑出 + 淡出
        x   = swap_exit_x * v / 128;
        opa = LV_OPA_COVER - LV_OPA_COVER * v / 128;
    } else {                                         // 后半:新内容从反向滑入 + 淡入
        if (!swapped) {
            cur = pending_app;
            lv_obj_add_flag(g_iconart, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(g_nextart, LV_OBJ_FLAG_HIDDEN);
            lv_obj_t *old = g_iconart; g_iconart = g_nextart; g_nextart = old;
            lv_label_set_text(g_name, tr_app_name(APPS[cur]->name));
            swapped = true;
        }
        int32_t w = v - 128;                         // 0..128
        x   = -swap_exit_x * (128 - w) / 128;
        opa = LV_OPA_COVER * w / 128;
    }
    lv_obj_set_style_translate_x(g_icon, x, 0);
    // Composite once so rounded stroke joins keep a uniform tone during the fade.
    lv_obj_set_style_opa_layered(g_icon, opa, 0);
    lv_obj_set_style_translate_x(g_name, x, 0);
    lv_obj_set_style_opa(g_name, opa, 0);
}

static void swap_completed(lv_anim_t *a) {
    (void)a;
    int dir = queued_dir; queued_dir = 0;
    if (dir) nav(dir);
}
static void nav(int dir) {
    if (lv_anim_get(&swap_exit_x, swap_exec)) { queued_dir = dir; return; }  // 最多缓存一个后续方向,快速连击也有响应
    pending_app = (cur + dir + APP_COUNT) % APP_COUNT;
    // 准备下一图标发生在动画开始前;中间帧只交换已建好的对象。
    launcher_icon_set(g_nextart,(launcher_icon_t)pending_app);
    swap_exit_x = -SWAP_SLIDE * dir;                   // 左滑(下一个)向左出,右滑反之
    swapped     = false;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &swap_exit_x);                 // 占位 var,仅用于识别动画句柄
    lv_anim_set_exec_cb(&a, swap_exec);
    lv_anim_set_values(&a, 0, 256);
    lv_anim_set_duration(&a, SWAP_MS);
    lv_anim_set_completed_cb(&a, swap_completed);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

static void launcher_gesture_cb(lv_event_t *e) {
    lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_active());
    if (d == LV_DIR_LEFT)       nav(+1);
    else if (d == LV_DIR_RIGHT) nav(-1);
}
static void arrow_prev_cb(lv_event_t *e) { nav(-1); }
static void arrow_next_cb(lv_event_t *e) { nav(+1); }

/* ---- 软件看门狗:盯 LVGL/渲染任务是否还在调度。卡死(LVGL 断言 halt、DMA 信号量永等、死循环等)
       ≥5s 就重启自恢复。硬件 panic 已配置成重启,但"纯卡死"不触发硬件看门狗,故补一个软的。 ---- */
static volatile uint32_t s_lvgl_hb;             // 心跳:由 app_tick_timer(LVGL 任务,20ms)累加
static void render_watchdog(void *arg) {
    (void)arg;
    uint32_t last = 0;
    int stuck = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (s_lvgl_hb == last) {
            if (++stuck >= 5) {                  // 连续 5s 无心跳 → 渲染任务卡死,重启
                ESP_LOGE("wdog", "LVGL task stalled >=5s -> restart");
                esp_restart();
            }
        } else { last = s_lvgl_hb; stuck = 0; }
    }
}

/* ---- 触摸加速(touch boost):有输入活动就把 CPU 钉到 240MHz,空闲 400ms 释放回 DFS。
       没有它,滑动/动画起步时 CPU 还在 80MHz,DFS 升频有迟滞 → 前几帧掉帧的"粘滞感"。 ---- */
static esp_pm_lock_handle_t s_boost;
static bool s_boosted;
static void touch_boost_poll(void) {
    if (!s_boost) return;
    bool want = lv_display_get_inactive_time(NULL) < 400;
    if (want != s_boosted) {
        s_boosted = want;
        if (want) esp_pm_lock_acquire(s_boost);
        else      esp_pm_lock_release(s_boost);
    }
}

/* ---- App 生命周期 ---- */
bool launcher_app_visible(void) {
    return cur_app && !lock_is_locked() && !quickpanel_is_open();
}
static void app_tick_timer(lv_timer_t *t) {
    (void)t;
    s_lvgl_hb++;
    touch_boost_poll();
    battery_visibility(battery_covered());
    if (!cur_app) return;
    bool visible = launcher_app_visible();
    uint32_t now = lv_tick_get();
    if (visible != s_app_visible) {
        buttons_reset_control();              // 遮挡/恢复不能重放之前的实体键事件
        s_app_visible = visible;
        s_tick_at = now;
        if (cur_app->visibility) cur_app->visibility(visible);
    }
    if (!cur_app->tick || (!visible && !cur_app->tick_in_background)) return;
    uint32_t period = cur_app->tick_period_ms ? cur_app->tick_period_ms : 50;
    if (now - s_tick_at < period) return;
    // 保留平均 50ms 的旧节拍,忙时不连跑补帧;动态页独立使用 20ms。
    s_tick_at += ((now - s_tick_at) / period) * period;
    cur_app->tick();
}
// 返回:先给当前 app 一次机会消费(如设置的二级子页 → 退回一级);没消费才退出 app 回启动器。
static void app_back(void) {
    if (cur_app && cur_app->back && cur_app->back()) return;
    go_home();
}
static void app_gesture_cb(lv_event_t *e) {
    if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_RIGHT) app_back();
}
static void back_cb(lv_event_t *e) { app_back(); }

static void enter_app(void) {
    cur_app = APPS[cur];
    battery_visibility(battery_covered());
    header_app_style();
    s_app_visible = true; s_tick_at = lv_tick_get();
    ESP_LOGI("app", "enter %s | free internal=%u psram=%u", cur_app->name,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));   // 盯住内部 RAM:依次开 app 应保持平稳,不再逐次掉
    app_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(app_screen, lv_color_black(), 0);
    lv_obj_remove_flag(app_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(app_screen, app_gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_label_set_text(g_title, tr_app_name(cur_app->name));   // 默认标题 = app 名(按语言)
    lv_obj_remove_flag(g_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g_back, LV_OBJ_FLAG_HIDDEN);
    if (cur_app == &app_audio || cur_app == &app_level) {
        lv_label_set_text(g_title, cur_app == &app_audio ? tools_text("AUDIO","音频") : tools_text("LEVEL","水平仪"));
        tools_label_center(g_title,233,63);
    }
    if (cur_app->enter) cur_app->enter(app_screen);     // app 可在 enter 里改标题(天气→城市)
    lv_screen_load(app_screen);
}

// 区分"点按进入"与"滑动切换":记下按下点,松手时位移很小才算点按 → 杜绝滑动时误进 app
static lv_point_t s_press_pt;
static void icon_pressed(lv_event_t *e) {
    lv_indev_t *id = lv_indev_active();
    if (id) lv_indev_get_point(id, &s_press_pt);
}
static void icon_released(lv_event_t *e) {
    lv_indev_t *id = lv_indev_active();
    if (!id) return;
    lv_point_t p;
    lv_indev_get_point(id, &p);
    int dx = p.x - s_press_pt.x, dy = p.y - s_press_pt.y;
    if (dx * dx + dy * dy > 22 * 22) return;            // 位移 >22px = 滑动,不进 app
    if (lv_anim_get(&swap_exit_x, swap_exec)) return;   // 切换动画进行中,忽略
    enter_app();
}

void launcher_set_title(const char *t) {
    if (g_title) lv_label_set_text(g_title, t);
}

void go_home(void) {
    if (!cur_app) return;
    lv_obj_add_flag(g_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_back, LV_OBJ_FLAG_HIDDEN);
    if (cur_app->exit) cur_app->exit();
    cur_app = NULL;
    battery_visibility(battery_covered());
    header_app_style();
    lv_screen_load(launcher_screen);
    if (app_screen) { lv_obj_delete_async(app_screen); app_screen = NULL; }
}

/* ---- 全局下拉:顶部边缘热区,任意界面从最顶下拉 → 打开快捷面板。
       热区挂 lv_layer_top();锁屏时表盘(也在 layer_top 且被移到最前)盖住它 → 锁屏不触发,正合要求。 ---- */
static void hotzone_gesture_cb(lv_event_t *e) {
    (void)e;
    if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_BOTTOM) quickpanel_open();
}

/* ---- 顶层悬浮:电量环 + 标题 + 返回 + 下拉热区 + 快捷面板 ---- */
static void build_overlay(void) {
    lv_obj_t *top = lv_layer_top();

    g_batt = lv_arc_create(top);
    lv_obj_set_size(g_batt, 458, 458);
    lv_obj_center(g_batt);
    lv_arc_set_rotation(g_batt, 270);
    lv_arc_set_bg_angles(g_batt, 0, 360);
    lv_arc_set_range(g_batt, 0, 100);
    lv_arc_set_value(g_batt, 0);                 // 真实电量由 battery_timer_cb 填
    lv_obj_remove_style(g_batt, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(g_batt, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(g_batt, lv_color_hex(0x15151a), LV_PART_MAIN);
    lv_obj_set_style_arc_width(g_batt, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(g_batt, lv_color_hex(COL_RING), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(g_batt, 8, LV_PART_INDICATOR);

    // 充电状态图标(⚡):顶端居中,默认隐藏。充电时呼吸、充满时常亮(只动这一小块)
    g_bolt = lv_label_create(top);
    lv_label_set_text(g_bolt, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_font(g_bolt, UI_FONT_SYM, 0);
    lv_obj_set_style_text_color(g_bolt, lv_color_hex(COL_RING), 0);
    lv_obj_align(g_bolt, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_add_flag(g_bolt, LV_OBJ_FLAG_HIDDEN);

    g_title = lv_label_create(top);
    lv_label_set_text(g_title, "");
    // ★必须显式设带中文 fallback 的字体:不设则继承 LV_FONT_DEFAULT(montserrat_14,无 fallback),
    //   中文标题(如"设置")渲染成占位方块 —— 实机踩过的坑。
    lv_obj_set_style_text_font(g_title, UI_FONT_L, 0);
    lv_obj_set_style_text_color(g_title, lv_color_hex(COL_TXT), 0);
    lv_obj_align(g_title, LV_ALIGN_TOP_MID, 0, 46);
    lv_obj_add_flag(g_title, LV_OBJ_FLAG_HIDDEN);

    // 返回键:圆形描边(Nothing 风)—— 深底 + 细灰环 + 白箭头
    g_back = lv_obj_create(top);
    lv_obj_set_size(g_back, 48, 48);
    lv_obj_set_style_radius(g_back, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_back, lv_color_hex(0x16161a), 0);
    lv_obj_set_style_bg_opa(g_back, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_back, 1, 0);
    lv_obj_set_style_border_color(g_back, lv_color_hex(COL_TXT2), 0);
    lv_obj_set_style_border_opa(g_back, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(g_back, 0, 0);
    lv_obj_align(g_back, LV_ALIGN_TOP_MID, -100, 40);
    lv_obj_remove_flag(g_back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_back, back_cb, LV_EVENT_CLICKED, NULL);
    g_back_label = lv_label_create(g_back);
    lv_label_set_text(g_back_label, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(g_back_label, lv_color_hex(COL_TXT), 0);
    lv_obj_center(g_back_label);
    g_backdots = lv_obj_create(g_back);
    lv_obj_remove_style_all(g_backdots);
    lv_obj_set_size(g_backdots, 48, 48);
    lv_obj_remove_flag(g_backdots, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_backdots, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_HIDDEN);
    glyph_line(g_backdots, 28, 17, 21, 24, 4, 1, COL_TXT);
    glyph_line(g_backdots, 21, 24, 28, 31, 4, 1, COL_TXT);
    lv_obj_add_flag(g_back, LV_OBJ_FLAG_HIDDEN);

    // 顶部边缘下拉热区(透明、仅最顶 30px):只在屏幕最顶起手的下拉才触发,避开列表纵向滚动误触
    lv_obj_t *hz = lv_obj_create(top);
    lv_obj_remove_style_all(hz);
    lv_obj_set_size(hz, lv_pct(100), 30);
    lv_obj_align(hz, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(hz, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(hz, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(hz, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(hz, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(hz, hotzone_gesture_cb, LV_EVENT_GESTURE, NULL);

    quickpanel_init(top);   // 全局快捷面板:挂 layer_top,初始隐藏在屏幕上方,等热区下拉
}

/* ---- 电量/充电状态:周期读 AXP2101,更新电量环颜色 + ⚡ 图标 ---- */
static void bolt_opa_cb(void *obj, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0); }

static void bolt_breath(bool on) {
    lv_anim_delete(g_bolt, bolt_opa_cb);
    if (!on) { lv_obj_set_style_opa(g_bolt, LV_OPA_COVER, 0); return; }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, g_bolt);
    lv_anim_set_exec_cb(&a, bolt_opa_cb);
    lv_anim_set_values(&a, LV_OPA_50, LV_OPA_COVER);
    lv_anim_set_duration(&a, 700);
    lv_anim_set_playback_duration(&a, 700);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

static void battery_visibility(bool covered) {
    static bool was_covered;
    if (covered == was_covered) return;
    was_covered = covered;
    if (covered) {
        lv_obj_add_flag(g_batt, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_bolt, LV_OBJ_FLAG_HIDDEN);
        bolt_breath(false);
    } else {
        lv_obj_remove_flag(g_batt, LV_OBJ_FLAG_HIDDEN);
        if (s_last_pwr == PWR_CHARGING || s_last_pwr == PWR_FULL)
            lv_obj_remove_flag(g_bolt, LV_OBJ_FLAG_HIDDEN);
        bolt_breath(s_last_pwr == PWR_CHARGING);
    }
}

static void battery_timer_cb(lv_timer_t *t) {
    int soc; pwr_state_t st;
    if (!power_read(&soc, &st)) return;          // 读失败:保持上次显示
    if (soc < 0)   soc = 0;
    if (soc > 100) soc = 100;
    lv_arc_set_value(g_batt, soc);

    uint32_t col;
    switch (st) {
        case PWR_CHARGING: col = COL_CHARGE; break;               // 充电:绿(⚡ 同步呼吸)
        case PWR_FULL:     col = COL_CHARGE; break;               // 充满:绿(⚡ 常亮)
        default:           col = (soc > 20) ? ((cur_app == &app_audio || cur_app == &app_level) ? TOOLS_WHITE : COL_TXT) // 放电:白;低电(≤20%)红
                                 : COL_WARN;
    }
    if (col != s_last_batt_color) {
        lv_obj_set_style_arc_color(g_batt, lv_color_hex(col), LV_PART_INDICATOR);
        lv_obj_set_style_text_color(g_bolt, lv_color_hex(col), 0);
        s_last_batt_color = col;
    }

    bool plugged = (st == PWR_CHARGING || st == PWR_FULL);
    if (plugged && !battery_covered()) lv_obj_remove_flag(g_bolt, LV_OBJ_FLAG_HIDDEN);
    else         lv_obj_add_flag(g_bolt, LV_OBJ_FLAG_HIDDEN);

    if (st != s_last_pwr) {
        bolt_breath(st == PWR_CHARGING && !battery_covered());         // 仅可见且充电时呼吸,充满则常亮
        s_last_pwr = st;
    }

    power_charge_govern();                        // 充电策略:按 die 温度自适应限流(凉快/温中/烫慢)
}

void launcher_start(void) {
    build_overlay();

    launcher_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(launcher_screen, lv_color_black(), 0);
    lv_obj_remove_flag(launcher_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(launcher_screen, launcher_gesture_cb, LV_EVENT_GESTURE, NULL);

    g_icon = lv_obj_create(launcher_screen);
    lv_obj_set_size(g_icon, ICON, ICON);
    lv_obj_set_style_radius(g_icon, ICON / 2, 0);
    lv_obj_set_style_border_width(g_icon, 2, 0);
    lv_obj_set_style_border_color(g_icon, lv_color_hex(LAUNCHER_FRAME_COLOR), 0);
    lv_obj_set_style_bg_opa(g_icon, LV_OPA_TRANSP, 0);   // 白色清晰外圈;可点击范围和位置保持
    lv_obj_align(g_icon, LV_ALIGN_CENTER, 0, -16);
    lv_obj_remove_flag(g_icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_icon, LV_OBJ_FLAG_EVENT_BUBBLE);   // 在图标上滑动也能切换
    lv_obj_add_event_cb(g_icon, icon_pressed,  LV_EVENT_PRESSED,  NULL);
    lv_obj_add_event_cb(g_icon, icon_released, LV_EVENT_RELEASED, NULL);

    g_iconart = launcher_icon_create(g_icon,LAUNCHER_WIFI);
    lv_obj_center(g_iconart);
    lv_obj_remove_flag(g_iconart, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_iconart, LV_OBJ_FLAG_EVENT_BUBBLE);

    g_nextart = launcher_icon_create(g_icon,LAUNCHER_WIFI);
    lv_obj_center(g_nextart);
    lv_obj_remove_flag(g_nextart, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_nextart, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_HIDDEN);

    g_name = lv_label_create(launcher_screen);
    lv_obj_set_style_text_color(g_name, lv_color_hex(COL_TXT), 0);
    lv_obj_set_style_text_font(g_name, &font_location_24, 0);
    lv_obj_set_width(g_name,300);
    lv_obj_set_style_text_align(g_name,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_align(g_name, LV_ALIGN_CENTER, 0, ICON / 2 + 14);

    lv_obj_t *al = launcher_icon_create(launcher_screen,LAUNCHER_PREV);
    lv_obj_align(al, LV_ALIGN_LEFT_MID, 14, 0);
    lv_obj_add_flag(al, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(al, 24);
    lv_obj_add_event_cb(al, arrow_prev_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ar = launcher_icon_create(launcher_screen,LAUNCHER_NEXT);
    lv_obj_align(ar, LV_ALIGN_RIGHT_MID, -14, 0);
    lv_obj_add_flag(ar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(ar, 24);
    lv_obj_add_event_cb(ar, arrow_next_cb, LV_EVENT_CLICKED, NULL);

    // 提高手势触发门槛(默认 50px → 64px),减少滑动误触切换
    for (lv_indev_t *id = lv_indev_get_next(NULL); id; id = lv_indev_get_next(id))
        if (lv_indev_get_type(id) == LV_INDEV_TYPE_POINTER)
            lv_indev_set_gesture_min_distance(id, 64);

    apply_app(0);
    lv_screen_load(launcher_screen);

    esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "touch", &s_boost);   // 触摸加速锁(touch_boost_poll)
    lv_timer_create(app_tick_timer, 20, NULL);   // 20ms 调度,app 各自节拍(兼喂软件看门狗)
    xTaskCreate(render_watchdog, "rwdt", 2560, NULL, configMAX_PRIORITIES - 2, NULL);   // 卡死自恢复

    power_init();
    battery_timer_cb(NULL);                       // 开机立即读一次电量
    lv_timer_create(battery_timer_cb, 2000, NULL);

    lock_init();                                  // 锁屏 / 表盘 / 实体键 / 省电
    lock_set(true);                               // 开机/烧录后默认进锁屏(表盘),BOOT 短按解锁
}
