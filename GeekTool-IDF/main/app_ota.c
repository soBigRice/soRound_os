// OTA 升级 —— ota_update.c 负责 HTTPS 下载/续传/校验,本页负责 worker 与圆屏状态。
// 线程:后台只发布加锁快照,UI 在 ota_tick(LVGL 任务)里读;离开页面仍继续更新。
// 安全:开了 bootloader 回滚(sdkconfig CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE)——
//   新固件启动后由 main.c 调 esp_ota_mark_app_valid_cancel_rollback() 确认;若新固件启动即崩,
//   下次复位 bootloader 自动回退旧分区。dual-OTA 分区(ota_0/ota_1)刷到另一个 slot,失败不毁当前固件。
#include "app.h"
#include "settings.h"
#include "glyph.h"
#include "ui_update.h"
#include "ota_update.h"
#include "esp_wifi.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_tls_errors.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

static const char *TAG = "ota";

// 云 OTA:Actions 发布 R2 → 专用镜像服务同步 → ota.miaozong.cc 的 HTTPS 静态直链。
// 镜像保留可信证书、no-store 和 Range/If-Match;设备验证完整根证书包及镜像。
// 双通道:stable=正式(v1.6 tag),beta=内测(v1.6-beta.1 tag)。CI 规则:正式 tag 两个对象都覆盖
// (正式对内测用户也是"最新"),beta tag 只覆盖 beta 对象 → 设备只需按开关二选一,无需比较版本新旧。
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static ota_status_t         s_status = { .state = OTA_IDLE };
static bool                 s_task_alive;
static int                  s_last_pct = -1;   // 每次进页面重放当前任务进度
static int                  s_last_attempt = -1;
static ota_state_t          s_shown = (ota_state_t)-1;

static lv_obj_t *g_status, *g_ver, *g_icon, *g_pctlbl, *g_progress;
static lv_obj_t *g_main, *g_hit, *g_switch, *g_channelbox, *g_gear, *g_settings;
static bool s_settings_open, s_visible;
static void ota_tick(void);

static ota_status_t status_snapshot(void) {
    portENTER_CRITICAL(&s_mux);
    ota_status_t result = s_status;
    portEXIT_CRITICAL(&s_mux);
    return result;
}
static void status_publish(const ota_status_t *status, void *user) {
    (void)user;
    portENTER_CRITICAL(&s_mux);
    s_status = *status;
    portEXIT_CRITICAL(&s_mux);
}

/* ===== Nothing:贴边点阵圆环 + 大点阵上箭头 + 底部细横条 =====
   圆环内放大图标与必要状态;launcher 在 OTA 页收起实线电量环,退出后恢复。 */
#define OTA_CX       233
#define OTA_CY       233
#define ORBIT_R      216
#define ICON_Y       122
#define IC_CX        108
#define IC_CY        108
#define ORBIT_N      104
#define OTA_AMBER    0xf5a623
#define OTA_BLUE     0x64d2ff
#define ORBIT_IDLE   0x98989c
#define ORBIT_DIM    0x343438

// Direct drawing avoids 213 styled dot objects competing with the TLS handshake heap.
static uint32_t s_orbit_colors[ORBIT_N];
static lv_opa_t s_orbit_opacity[ORBIT_N];
static int s_icon_kind;
static uint32_t s_icon_color;
static int s_ring_pct, s_orbit_phase;

static void draw_dot(lv_layer_t *layer, int x, int y, int r, uint32_t color, lv_opa_t opacity) {
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE; d.bg_color = lv_color_hex(color); d.bg_opa = opacity;
    lv_area_t a = {x-r, y-r, x+r-1, y+r-1};
    lv_draw_rect(layer, &d, &a);
}

static void orbit_draw(lv_event_t *e) {
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t bounds; lv_obj_get_coords(lv_event_get_target_obj(e), &bounds);
    for (int i = 0; i < ORBIT_N; i++) {
        float a = -1.57079633f + 6.28318531f * i / ORBIT_N;
        draw_dot(layer, bounds.x1 + OTA_CX + (int)(cosf(a) * ORBIT_R),
                 bounds.y1 + OTA_CY + (int)(sinf(a) * ORBIT_R), 3,
                 s_orbit_colors[i], s_orbit_opacity[i]);
    }
}

static void set_visible(lv_obj_t *o, bool visible) {
    if (!o) return;
    if (visible) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else         lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void arrow_anim_exec(void *o, int32_t v) {
    // 只向上运行;首尾淡入/淡出,循环复位发生在不可见时,不播放向下回弹。
    lv_obj_set_y(o, ICON_Y + 12 - v * 36 / 1000);
    int opa = v < 160 ? v * 255 / 160 : v > 840 ? (1000 - v) * 255 / 160 : 255;
    lv_obj_set_style_opa(o, (lv_opa_t)opa, 0);
}

static void stop_arrow_anim(void) {
    if (!g_icon) return;
    lv_anim_delete(g_icon, arrow_anim_exec);
    lv_obj_set_y(g_icon, ICON_Y);
    lv_obj_set_style_opa(g_icon, LV_OPA_COVER, 0);
}

static void start_arrow_anim(void) {
    if (!g_icon || !s_visible || s_settings_open) return;
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, g_icon);
    lv_anim_set_exec_cb(&a, arrow_anim_exec);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, 1500);
    lv_anim_set_repeat_delay(&a, 120);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

static uint32_t state_color(ota_state_t state) {
    switch (state) {
        case OTA_RUNNING: case OTA_FAIL: return COL_RED;
        case OTA_RETRYING: return OTA_AMBER;
        case OTA_VERIFYING: return OTA_BLUE;
        case OTA_OK: case OTA_UPTODATE: return COL_CHARGE;
        default: return COL_TXT;
    }
}

static bool orbit_active(ota_state_t state) {
    return state == OTA_CHECKING || state == OTA_HEADER || state == OTA_RUNNING ||
           state == OTA_RETRYING || state == OTA_VERIFYING;
}

static void orbit_render(ota_state_t state) {
    uint32_t accent = state_color(state);
    int filled = s_ring_pct * ORBIT_N / 100;
    int head = filled > 0 ? filled - 1 : 0;
    int pulse = s_orbit_phase * 2;
    if (pulse > ORBIT_N) pulse = 2 * ORBIT_N - pulse;
    for (int i = 0; i < ORBIT_N; i++) {
        uint32_t col = ORBIT_DIM;
        lv_opa_t opa = LV_OPA_COVER;
        if (state == OTA_IDLE) col = ORBIT_IDLE;
        else if (state == OTA_CHECKING || state == OTA_HEADER) {
            int tail = (s_orbit_phase - i + ORBIT_N) % ORBIT_N;
            if (tail < 6) { col = accent; opa = (lv_opa_t)(255 - tail * 32); }
        } else if (state == OTA_OK || state == OTA_UPTODATE) col = accent;
        else if (state == OTA_FAIL) { col = accent; opa = LV_OPA_50; }
        else {
            if (i < filled) col = accent;
            // 只呼吸已经点亮的末端;不把未下载部分点亮成“假进度”。
            if (filled > 0 && i == head) opa = (lv_opa_t)(128 + pulse * 127 / ORBIT_N);
        }
        s_orbit_colors[i] = col;
        s_orbit_opacity[i] = opa;
    }
    lv_obj_invalidate(g_main);
}

static void orbit_anim_exec(void *o, int32_t phase) {
    (void)o;
    s_orbit_phase = phase;
    orbit_render(s_shown);
}

static void stop_orbit_anim(void) {
    if (g_main) lv_anim_delete(g_main, orbit_anim_exec);
}

static void start_orbit_anim(void) {
    if (!g_main || !s_visible || s_settings_open || !orbit_active(s_shown)) return;
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, g_main);
    lv_anim_set_exec_cb(&a, orbit_anim_exec);
    lv_anim_set_values(&a, 0, ORBIT_N - 1);
    lv_anim_set_duration(&a, 1800);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

static void draw_up_arrow(lv_layer_t *layer, int x0, int y0, uint32_t col) {
    // 等间距点阵填充箭头:15 列的对称箭头头部,5 列宽杆身,不以三条细线拼轮廓。
    for (int row = 0; row < 17; row++) {
        int half = row < 8 ? row : 2;
        for (int x = -half; x <= half; x++)
            draw_dot(layer, x0 + IC_CX + x * 12, y0 + 15 + row * 12, 4, col, LV_OPA_COVER);
    }
}
static void draw_line_dots(lv_layer_t *layer, int x0, int y0, int x1, int y1, uint32_t col) {
    int count = (int)(sqrtf((float)((x1-x0)*(x1-x0) + (y1-y0)*(y1-y0))) / 12);
    if (count < 1) count = 1;
    for (int i = 0; i <= count; i++)
        draw_dot(layer, x0+(x1-x0)*i/count, y0+(y1-y0)*i/count, 4, col, LV_OPA_COVER);
}
static void icon_draw(lv_event_t *e) {
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t bounds; lv_obj_get_coords(lv_event_get_target_obj(e), &bounds);
    int x = bounds.x1 + IC_CX, y = bounds.y1 + IC_CY;
    if (s_icon_kind == 0) draw_up_arrow(layer, bounds.x1, bounds.y1, s_icon_color);
    else if (s_icon_kind == 1) {
        draw_line_dots(layer, x-64, y, x-20, y+50, s_icon_color);
        draw_line_dots(layer, x-20, y+50, x+72, y-52, s_icon_color);
    } else {
        draw_line_dots(layer, x-62, y-62, x+62, y+62, s_icon_color);
        draw_line_dots(layer, x+62, y-62, x-62, y+62, s_icon_color);
    }
}
static void set_icon(int kind, uint32_t col) {     // 0=上箭头 1=对勾 2=叉
    s_icon_kind = kind; s_icon_color = col;
    lv_obj_invalidate(g_icon);
}

static void ota_task(void *arg) {
    wifi_ps_type_t previous;
    bool restore = esp_wifi_get_ps(&previous) == ESP_OK && esp_wifi_set_ps(WIFI_PS_NONE) == ESP_OK;
    ota_status_t result = ota_update_run(arg, status_publish, NULL);
    ESP_LOGI(TAG, "result state=%d version=%s", result.state, result.version);
    if (restore) {
        esp_err_t err = esp_wifi_set_ps(previous);
        if (err != ESP_OK) ESP_LOGW(TAG, "restore WiFi power save: %s", esp_err_to_name(err));
    }
    // 成功后仍占有任务直到重启,不能在这 1.2 秒再次启动下载。
    if (result.state == OTA_OK) { vTaskDelay(pdMS_TO_TICKS(1200)); esp_restart(); }
    portENTER_CRITICAL(&s_mux);
    s_task_alive = false;
    portEXIT_CRITICAL(&s_mux);
    vTaskDelete(NULL);
}

static void beta_changed(lv_event_t *e) {             // 内测通道开关:开=收 beta+正式,关=只收正式
    portENTER_CRITICAL(&s_mux);
    bool busy = s_task_alive;
    portEXIT_CRITICAL(&s_mux);
    if (busy) {
        if (settings_beta()) lv_obj_add_state(g_switch, LV_STATE_CHECKED);
        else lv_obj_remove_state(g_switch, LV_STATE_CHECKED);
        return;
    }
    bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    settings_set_beta(on ? 1 : 0);
    settings_save();
}

static void settings_btn(lv_event_t *e) {
    (void)e;
    s_settings_open = true;
    stop_arrow_anim();
    stop_orbit_anim();
    set_visible(g_main, false);
    set_visible(g_gear, false);
    set_visible(g_settings, true);
    launcher_set_title(tr_app_name("settings"));
}

static bool ota_back(void) {
    if (!s_settings_open) return false;
    s_settings_open = false;
    set_visible(g_settings, false);
    set_visible(g_main, true);
    set_visible(g_gear, true);
    launcher_set_title(tr(S_OTA_TITLE));
    s_shown = (ota_state_t)-1;
    ota_tick();
    return true;
}

static void start_btn(lv_event_t *e) {
    (void)e;
    if (!wifi_service_enabled()) {
        lv_label_set_text(g_status, tr(S_CONNECT_WIFI));
        lv_obj_set_style_text_color(g_status, lv_color_hex(COL_RED), 0);
        return;
    }
    portENTER_CRITICAL(&s_mux);
    if (s_task_alive) { portEXIT_CRITICAL(&s_mux); return; }
    s_task_alive = true;
    s_status = (ota_status_t){ .state = OTA_CHECKING, .attempt = 1 };
    portEXIT_CRITICAL(&s_mux);
    s_last_pct = -1;
    lv_label_set_text(g_pctlbl, "");
    ota_tick();                                      // 先禁用操作,并让快速失败也能重新渲染错误
    const char *url = settings_beta() ? OTA_URL_BETA : OTA_URL_STABLE;
    if (xTaskCreate(ota_task, "ota", 8192, (void *)url, 5, NULL) != pdPASS) {
        ota_status_t failed = { .state = OTA_FAIL, .failed_at = OTA_CHECKING, .error = ESP_ERR_NO_MEM };
        portENTER_CRITICAL(&s_mux);
        s_task_alive = false; s_status = failed;
        portEXIT_CRITICAL(&s_mux);
    }
}

static void ota_enter(lv_obj_t *parent) {
    // OTA task 可以在离开页面后继续。这里不能重置后台快照,否则重进时会显示 idle,
    // 但 start_btn 又因 s_task_alive 拒绝点击,形成“后台在下、前台像卡住”的假状态。
    // 只重置 UI 去重缓存,让首个 tick 把当前后台状态完整重放到新控件。
    s_last_pct = -1;
    s_last_attempt = -1;
    s_shown = (ota_state_t)-1;
    s_settings_open = false;
    s_visible = true;
    s_ring_pct = s_orbit_phase = 0;
    launcher_set_title(tr(S_OTA_TITLE));

    g_main = lv_obj_create(parent);
    lv_obj_remove_style_all(g_main);
    lv_obj_set_size(g_main, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(g_main, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_main, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_add_event_cb(g_main, orbit_draw, LV_EVENT_DRAW_MAIN, NULL);

    g_icon = lv_obj_create(g_main);
    lv_obj_remove_style_all(g_icon);
    lv_obj_set_size(g_icon, 216, 216);
    lv_obj_align(g_icon, LV_ALIGN_TOP_MID, 0, ICON_Y);
    lv_obj_remove_flag(g_icon, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_icon, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(g_icon, icon_draw, LV_EVENT_DRAW_MAIN, NULL);

    // 下载期间箭头保留;细横条和小百分比放在下方,仍处于外环内的圆屏安全区。
    g_progress = lv_bar_create(g_main);
    lv_obj_set_size(g_progress, 116, 4);
    lv_obj_align(g_progress, LV_ALIGN_TOP_MID, -26, 391);
    lv_bar_set_range(g_progress, 0, 100);
    lv_obj_set_style_radius(g_progress, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(g_progress, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_progress, lv_color_hex(0x242428), LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_progress, lv_color_hex(COL_RED), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(g_progress, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_progress, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(g_progress, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_progress, 0, LV_PART_MAIN);

    g_pctlbl = lv_label_create(g_main);
    lv_obj_set_style_text_font(g_pctlbl, UI_FONT_M, 0);
    lv_obj_set_style_text_color(g_pctlbl, lv_color_hex(COL_TXT2), 0);
    lv_label_set_text(g_pctlbl, "");
    lv_obj_align(g_pctlbl, LV_ALIGN_TOP_MID, 70, 385);

    // 状态说明:只承载反馈和异常原因,不再承担主操作说明。
    g_status = lv_label_create(g_main);
    lv_label_set_long_mode(g_status, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(g_status, 280);
    lv_obj_set_style_text_align(g_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_status, UI_FONT_SYM, 0);
    lv_label_set_text(g_status, "");
    lv_obj_set_style_text_color(g_status, lv_color_hex(COL_TXT2), 0);
    lv_obj_align(g_status, LV_ALIGN_TOP_MID, 0, 358);

    // 整个圆环内可点,小齿轮另有 48px 热区。
    g_hit = lv_obj_create(g_main);
    lv_obj_remove_style_all(g_hit);
    lv_obj_set_size(g_hit, ORBIT_R * 2, ORBIT_R * 2);
    lv_obj_align(g_hit, LV_ALIGN_TOP_MID, 0, OTA_CY - ORBIT_R);
    lv_obj_set_style_radius(g_hit, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_hit, lv_color_hex(0x16161c), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(g_hit, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_remove_flag(g_hit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_hit, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(g_hit, start_btn, LV_EVENT_CLICKED, NULL);

    g_gear = lv_obj_create(parent);
    lv_obj_remove_style_all(g_gear);
    lv_obj_set_size(g_gear, 48, 48);
    lv_obj_align(g_gear, LV_ALIGN_TOP_MID, 100, 40);
    lv_obj_set_style_radius(g_gear, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_gear, lv_color_hex(0x16161c), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(g_gear, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_remove_flag(g_gear, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_gear, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(g_gear, settings_btn, LV_EVENT_CLICKED, NULL);
    glyph_circle(g_gear, 24, 24, 7, 4, 1, COL_TXT2);
    static const int teeth[8][2]={{24,13},{32,16},{35,24},{32,32},{24,35},{16,32},{13,24},{16,16}};
    for (int i=0;i<8;i++) glyph_dot(g_gear,teeth[i][0],teeth[i][1],2,COL_TXT2);

    // 复用既有通道/NVS 契约,只把开关移到 OTA 内部设置子页。
    g_settings = lv_obj_create(parent);
    lv_obj_remove_style_all(g_settings);
    lv_obj_set_size(g_settings, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(g_settings, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_settings, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_HIDDEN);
    // 版本移入设置,主界面只保留大圆圈和点阵箭头。
    g_ver = lv_label_create(g_settings);
    lv_label_set_text(g_ver, esp_app_get_description()->version);
    lv_obj_set_style_text_font(g_ver, UI_FONT_M, 0);
    lv_obj_set_style_text_color(g_ver, lv_color_hex(COL_TXT2), 0);
    lv_obj_align(g_ver, LV_ALIGN_TOP_MID, 0, 132);
    g_channelbox = lv_obj_create(g_settings);
    lv_obj_set_size(g_channelbox, 300, 72);
    lv_obj_align(g_channelbox, LV_ALIGN_TOP_MID, 0, 184);
    lv_obj_set_style_radius(g_channelbox, 18, 0);
    lv_obj_set_style_bg_color(g_channelbox, lv_color_hex(0x141419), 0);
    lv_obj_set_style_bg_opa(g_channelbox, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_channelbox, 0, 0);
    lv_obj_set_style_pad_all(g_channelbox, 0, 0);
    lv_obj_remove_flag(g_channelbox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_channelbox, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *bt = lv_label_create(g_channelbox);
    lv_obj_set_style_text_font(bt, UI_FONT_SYM, 0);
    lv_obj_set_style_text_color(bt, lv_color_hex(COL_TXT), 0);
    lv_label_set_text(bt, tr(S_BETA_CH));
    lv_obj_align(bt, LV_ALIGN_LEFT_MID, 24, 0);

    g_switch = lv_switch_create(g_channelbox);
    lv_obj_set_size(g_switch, 54, 28);
    lv_obj_align(g_switch, LV_ALIGN_RIGHT_MID, -24, 0);
    lv_obj_set_style_bg_color(g_switch, lv_color_hex(0x2a2a31), LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_switch, lv_color_hex(COL_TXT2), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(g_switch, lv_color_hex(COL_TXT), LV_PART_KNOB);
    lv_obj_remove_flag(g_switch, LV_OBJ_FLAG_GESTURE_BUBBLE);
    if (settings_beta()) lv_obj_add_state(g_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(g_switch, beta_changed, LV_EVENT_VALUE_CHANGED, NULL);

    ota_tick();                  // 首帧立即还原空闲或后台任务真实状态,避免短暂闪错。
}

static void ota_tick(void) {
    if (!g_status) return;
    ota_status_t status = status_snapshot();
    bool show_progress = status.state == OTA_RUNNING || status.state == OTA_VERIFYING || status.state == OTA_RETRYING;
    if (show_progress && status.pct != s_last_pct) {
        s_last_pct = status.pct;
        int pct = status.pct < 0 ? 0 : status.pct > 100 ? 100 : status.pct;
        s_ring_pct = pct;
        lv_bar_set_value(g_progress, pct, LV_ANIM_OFF);
        char pb[8]; snprintf(pb, sizeof pb, "%d%%", pct);
        lv_label_set_text(g_pctlbl, pb);
        orbit_render(status.state);
    }
    if (status.state == s_shown && status.attempt == s_last_attempt) return;
    s_shown = status.state; s_last_attempt = status.attempt;
    stop_arrow_anim();
    stop_orbit_anim();
    s_orbit_phase = 0;
    orbit_render(status.state);
    lv_obj_set_style_bg_color(g_progress, lv_color_hex(state_color(status.state)), LV_PART_INDICATOR);
    bool busy = status.state == OTA_CHECKING || status.state == OTA_HEADER || status.state == OTA_RUNNING ||
                status.state == OTA_RETRYING || status.state == OTA_VERIFYING || status.state == OTA_OK;
    if (busy) {
        lv_obj_remove_flag(g_hit, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_state(g_switch, LV_STATE_DISABLED);
    } else {
        lv_obj_add_flag(g_hit, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_state(g_switch, LV_STATE_DISABLED);
    }
    set_visible(g_progress, show_progress);
    set_visible(g_pctlbl, show_progress);
    lv_obj_set_style_text_color(g_status, lv_color_hex(COL_TXT2), 0);
    // 状态占据箭头与横条之间的安全区,避免长英文在圆屏底部碰到点阵环或被裁切。
    lv_obj_set_y(g_status, show_progress ? 344 : 358);
    switch (status.state) {
        case OTA_IDLE:
            set_icon(0, COL_TXT);
            lv_label_set_text(g_status, "");
            start_arrow_anim();
            break;
        case OTA_HEADER:
        case OTA_CHECKING:
            set_icon(0, COL_TXT);
            lv_label_set_text(g_status, tr(S_CHECKING));
            start_arrow_anim();
            break;
        case OTA_RETRYING: {
            set_icon(0, OTA_AMBER);
            lv_obj_set_style_text_color(g_status, lv_color_hex(OTA_AMBER), 0);
            char b[96]; snprintf(b, sizeof b, "%s\n%d/%d", tr(S_OTA_RETRY),
                                 status.attempt + 1, OTA_UPDATE_ATTEMPTS);
            lv_label_set_text(g_status, b);
            start_arrow_anim();
            break;
        }
        case OTA_RUNNING:
            set_icon(0, COL_TXT);
            lv_label_set_text(g_status, tr(S_OTA_KEEP_POWER));
            start_arrow_anim();
            break;
        case OTA_VERIFYING:
            set_icon(0, OTA_BLUE);
            lv_obj_set_style_text_color(g_status, lv_color_hex(OTA_BLUE), 0);
            lv_label_set_text(g_status, tr(S_OTA_VERIFYING));
            start_arrow_anim();
            break;
        case OTA_OK:
            set_icon(1, COL_CHARGE);
            lv_obj_set_style_text_color(g_status, lv_color_hex(COL_CHARGE), 0);
            lv_label_set_text(g_status, tr(S_DONE_REBOOT));
            break;
        case OTA_UPTODATE: {
            set_icon(1, COL_CHARGE);
            lv_obj_set_style_text_color(g_status, lv_color_hex(COL_CHARGE), 0);
            char b[96]; snprintf(b, sizeof b, "%s\n%s", tr(S_UPTODATE), status.version);
            lv_label_set_text(g_status, b);
            break;
        }
        case OTA_FAIL: {
            set_icon(2, COL_RED);
            str_id_t message = status.failed_at == OTA_HEADER ? S_OTA_HEADER_FAIL :
                status.failed_at == OTA_RUNNING ? S_OTA_DOWNLOAD_FAIL :
                status.failed_at == OTA_VERIFYING ? S_OTA_VERIFY_FAIL : S_OTA_CONNECT_FAIL;
            if (status.tls_flags || ota_tls_certificate_error(status.tls_code)) message = S_OTA_TLS_FAIL;
            char b[128];
            if (status.http_status >= 400)
                snprintf(b, sizeof b, "%s\nHTTP %d", tr(message), status.http_status);
            else if(status.transport_error==ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME)
                snprintf(b,sizeof b,"%s\nDNS",tr(message));
            else if (status.tls_code)
                snprintf(b, sizeof b, "%s\nTLS -0x%04x", tr(message), ota_tls_error_magnitude(status.tls_code));
            else if(status.socket_errno)
                snprintf(b,sizeof b,"%s\nTCP %d",tr(message),status.socket_errno);
            else snprintf(b, sizeof b, "%s\n0x%04x", tr(message), (unsigned)status.error);
            lv_label_set_text(g_status, b);
            lv_obj_set_style_text_color(g_status, lv_color_hex(COL_RED), 0);
            break;
        }
    }
    start_orbit_anim();
}

static void ota_exit(void) {
    stop_arrow_anim();                           // 后台下载独立于页面,只停本页视觉动画。
    stop_orbit_anim();
    g_status = g_ver = g_icon = g_pctlbl = g_progress = NULL;
    g_main = g_hit = g_switch = g_channelbox = g_gear = g_settings = NULL;
    s_settings_open = s_visible = false;
}

static void ota_visibility(bool visible) {
    s_visible = visible;
    stop_arrow_anim();
    stop_orbit_anim();
    if (visible) {
        s_shown = (ota_state_t)-1; s_last_pct = -1;
        ota_tick();
    }
}

const app_t app_ota = { "ota", COL_TXT, ota_enter, ota_tick, ota_exit, ota_back, 0, ota_visibility };
