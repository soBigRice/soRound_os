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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "ota";

// 云 OTA:Cloudflare R2 自定义域名直链(国内可达,GitHub release 资产国内常被墙)。
// 打 v* tag → Actions 构建 → 传 GeekTool.bin 到 R2 bucket(覆盖同名对象)→ 设备点 update 即拉最新。
//   直链无跳转、无鉴权、HTTPS(Cloudflare 证书在 FULL crt_bundle 里)。
//   上传时带 Cache-Control:no-store,边缘不缓存 → 永远拉最新(CI 那步已设,无需后台缓存规则)。
//   本地测试想用局域网 HTTP,临时改回 http://<你电脑IP>:8000/GeekTool.bin(build 目录起 http.server)。
// 双通道:stable=正式(v1.6 tag),beta=内测(v1.6-beta.1 tag)。CI 规则:正式 tag 两个对象都覆盖
// (正式对内测用户也是"最新"),beta tag 只覆盖 beta 对象 → 设备只需按开关二选一,无需比较版本新旧。
#define OTA_URL_STABLE "https://ota.miaozong.cc/GeekTool.bin"
#define OTA_URL_BETA   "https://ota.miaozong.cc/GeekTool-beta.bin"

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static ota_status_t         s_status = { .state = OTA_IDLE };
static bool                 s_task_alive;
static int                  s_last_pct = -1;   // 每次进页面重放当前任务进度
static int                  s_last_attempt = -1;
static ota_state_t          s_shown = (ota_state_t)-1;

static lv_obj_t *g_status, *g_ver, *g_icon, *g_pctlbl, *g_action;
static lv_obj_t *g_orbitbox, *g_hit, *g_switch, *g_channelbox;
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

/* ===== Orbit Console:开放点阵轨道 + 中央状态 + 底部通道胶囊 =====
   全局 layer_top 已有 458px 电量环,OTA 页不能再画完整内环,否则真机会形成双重“靶心”。 */
#define OTA_CX       233
#define OTA_CY       232
#define ORBIT_R      120
#define ORBIT_N      54
#define ORBIT_DOT_R  3
#define ORBIT_A0     (-0.698132f)   // -40°,右上端点
#define ORBIT_A1     ( 3.839724f)   // 220°,左上端点;顶部留 100° 开口给版本信息

#define IC_CX 60           // g_icon 容器 120x104,中心 (60,52)
#define IC_CY 52

static lv_obj_t *g_orbit[ORBIT_N];

static void set_visible(lv_obj_t *o, bool visible) {
    if (!o) return;
    if (visible) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else         lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void orbit_dot_style(int i, uint32_t col, lv_opa_t opa) {
    if (!g_orbit[i]) return;
    ui_bg_color(g_orbit[i], col);
    ui_bg_opa(g_orbit[i], opa);
}

// 轨道阅读方向从左上端点走向右上端点;数组坐标的生成方向相反,这里做一次映射。
static int orbit_idx(int step) { return ORBIT_N - 1 - step; }

static void orbit_dim(void) {
    for (int i = 0; i < ORBIT_N; i++) orbit_dot_style(i, COL_TXT, LV_OPA_20);
}

static void orbit_idle(void) {
    for (int step = 0; step < ORBIT_N; step++)
        orbit_dot_style(orbit_idx(step), step < 5 ? COL_RED : COL_TXT,
                        step < 5 ? (lv_opa_t)(255 - step * 38) : LV_OPA_COVER);
}

static void orbit_progress(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int head = pct * (ORBIT_N - 1) / 100;
    for (int step = 0; step < ORBIT_N; step++)
        orbit_dot_style(orbit_idx(step), step == head ? COL_RED : COL_TXT,
                        step <= head ? LV_OPA_COVER : LV_OPA_20);
}

static void orbit_check_frame(int phase) {
    for (int step = 0; step < ORBIT_N; step++) {
        int tail = phase - step;
        bool lit = tail >= 0 && tail <= 5;
        orbit_dot_style(orbit_idx(step), tail == 0 ? COL_RED : COL_TXT,
                        lit ? (lv_opa_t)(80 + (5 - tail) * 35) : LV_OPA_20);
    }
}

static void orbit_anim_exec(void *o, int32_t v) {
    (void)o;
    if (g_orbitbox && (s_shown == OTA_CHECKING || s_shown == OTA_HEADER || s_shown == OTA_RETRYING)) orbit_check_frame(v);
}

static void stop_orbit_anim(void) {
    if (g_orbitbox) lv_anim_delete(g_orbitbox, orbit_anim_exec);
}

static void start_orbit_anim(void) {
    stop_orbit_anim();
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, g_orbitbox);
    lv_anim_set_exec_cb(&a, orbit_anim_exec);
    lv_anim_set_values(&a, 0, ORBIT_N - 1);
    lv_anim_set_duration(&a, 1600);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

static void draw_dl_arrow(uint32_t col) {          // 下载箭头:竖杆 + 箭头 + 底托
    glyph_line(g_icon, IC_CX, IC_CY - 34, IC_CX, IC_CY + 14, 8, 3, col);       // 竖杆
    glyph_line(g_icon, IC_CX, IC_CY + 16, IC_CX - 22, IC_CY - 8, 8, 3, col);   // 左斜
    glyph_line(g_icon, IC_CX, IC_CY + 16, IC_CX + 22, IC_CY - 8, 8, 3, col);   // 右斜
    glyph_line(g_icon, IC_CX - 28, IC_CY + 34, IC_CX + 28, IC_CY + 34, 9, 3, col); // 底托
}
static void draw_check(uint32_t col) {             // 对勾
    glyph_line(g_icon, IC_CX - 26, IC_CY, IC_CX - 6, IC_CY + 22, 7, 3, col);
    glyph_line(g_icon, IC_CX - 6, IC_CY + 22, IC_CX + 28, IC_CY - 20, 7, 3, col);
}
static void draw_cross(uint32_t col) {             // 叉
    glyph_line(g_icon, IC_CX - 22, IC_CY - 22, IC_CX + 22, IC_CY + 22, 7, 3, col);
    glyph_line(g_icon, IC_CX + 22, IC_CY - 22, IC_CX - 22, IC_CY + 22, 7, 3, col);
}
static void set_icon(int kind, uint32_t col) {     // 0=箭头 1=对勾 2=叉 3=空(下载显数字)
    lv_obj_clean(g_icon);
    if (kind == 0) draw_dl_arrow(col);
    else if (kind == 1) draw_check(col);
    else if (kind == 2) draw_cross(col);
}

static void ota_task(void *arg) {
    wifi_ps_type_t previous;
    bool restore = esp_wifi_get_ps(&previous) == ESP_OK && esp_wifi_set_ps(WIFI_PS_NONE) == ESP_OK;
    ota_status_t result = ota_update_run(arg, status_publish, NULL);
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
    if (busy) return;
    bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    settings_set_beta(on ? 1 : 0);
    settings_save();
}

static void start_btn(lv_event_t *e) {
    (void)e;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {    // 没连 WiFi
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
    orbit_idle();                                      // 重新检查时清掉上一轮终态
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
    launcher_set_title(tr(S_OTA_TITLE));

    // 当前版本:紧跟全局标题,占用开放轨道顶部留口,不再压在线条上。
    g_ver = lv_label_create(parent);
    const esp_app_desc_t *d = esp_app_get_description();
    char vb[64]; snprintf(vb, sizeof vb, "%s  %s", tr(S_CURRENT), d->version);
    lv_label_set_text(g_ver, vb);
    lv_obj_set_style_text_font(g_ver, UI_FONT_M, 0);
    lv_obj_set_style_text_color(g_ver, lv_color_hex(COL_TXT2), 0);
    lv_obj_align(g_ver, LV_ALIGN_TOP_MID, 0, 102);

    // 开放式点阵轨道:只承担流程与下载进度,不与全局电量环竞争。
    g_orbitbox = lv_obj_create(parent);
    lv_obj_remove_style_all(g_orbitbox);
    lv_obj_set_size(g_orbitbox, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(g_orbitbox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_orbitbox, LV_OBJ_FLAG_EVENT_BUBBLE);
    for (int i = 0; i < ORBIT_N; i++) {
        float a = ORBIT_A0 + (ORBIT_A1 - ORBIT_A0) * i / (ORBIT_N - 1);
        int x = OTA_CX + (int)(cosf(a) * ORBIT_R);
        int y = OTA_CY + (int)(sinf(a) * ORBIT_R);
        g_orbit[i] = glyph_dot(g_orbitbox, x, y, ORBIT_DOT_R, COL_TXT);
        lv_obj_set_style_bg_opa(g_orbit[i], LV_OPA_20, 0);
    }

    // 中央点阵图标:静态邀请点击;检查态由轨道运动表达,避免原来无目的的上下弹跳。
    g_icon = lv_obj_create(parent);
    lv_obj_remove_style_all(g_icon);
    lv_obj_set_size(g_icon, 120, 104);
    lv_obj_align(g_icon, LV_ALIGN_TOP_MID, 0, 158);
    lv_obj_add_flag(g_icon, LV_OBJ_FLAG_EVENT_BUBBLE);

    // 中心大百分比(下载时替代图标)
    g_pctlbl = lv_label_create(parent);
    lv_obj_set_style_text_font(g_pctlbl, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(g_pctlbl, lv_color_hex(COL_TXT), 0);
    lv_label_set_text(g_pctlbl, "");
    lv_obj_align(g_pctlbl, LV_ALIGN_TOP_MID, 0, 191);

    // 主操作文案始终位于图标下方,空闲/失败/已最新时均可直接再次点击。
    g_action = lv_label_create(parent);
    lv_obj_set_style_text_font(g_action, UI_FONT_L, 0);
    lv_obj_set_style_text_color(g_action, lv_color_hex(COL_TXT), 0);
    lv_obj_set_style_text_align(g_action, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(g_action, tr(S_TAP_UPDATE));
    lv_obj_align(g_action, LV_ALIGN_TOP_MID, 0, 274);

    // 状态说明:只承载反馈和异常原因,不再承担主操作说明。
    g_status = lv_label_create(parent);
    lv_label_set_long_mode(g_status, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(g_status, 280);
    lv_obj_set_style_text_align(g_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_status, UI_FONT_M, 0);
    lv_label_set_text(g_status, "");
    lv_obj_set_style_text_color(g_status, lv_color_hex(COL_TXT2), 0);
    lv_obj_align(g_status, LV_ALIGN_TOP_MID, 0, 309);

    // 透明主操作热区覆盖图标和文案,但不遮挡底部通道。
    g_hit = lv_obj_create(parent);
    lv_obj_remove_style_all(g_hit);
    lv_obj_set_size(g_hit, 214, 178);
    lv_obj_align(g_hit, LV_ALIGN_TOP_MID, 0, 145);
    lv_obj_set_style_radius(g_hit, 80, 0);
    lv_obj_set_style_bg_color(g_hit, lv_color_hex(0x16161c), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(g_hit, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_remove_flag(g_hit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_hit, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(g_hit, start_btn, LV_EVENT_CLICKED, NULL);

    // 测试通道收进统一胶囊,与主操作拉开层级;下载期间禁用,避免通道语义中途改变。
    g_channelbox = lv_obj_create(parent);
    lv_obj_set_size(g_channelbox, 192, 48);
    lv_obj_align(g_channelbox, LV_ALIGN_BOTTOM_MID, 0, -38);
    lv_obj_set_style_radius(g_channelbox, 24, 0);
    lv_obj_set_style_bg_color(g_channelbox, lv_color_hex(0x141419), 0);
    lv_obj_set_style_bg_opa(g_channelbox, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_channelbox, 1, 0);
    lv_obj_set_style_border_color(g_channelbox, lv_color_hex(0x2d2d34), 0);
    lv_obj_set_style_border_opa(g_channelbox, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(g_channelbox, 0, 0);
    lv_obj_remove_flag(g_channelbox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_channelbox, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *bt = lv_label_create(g_channelbox);
    lv_obj_set_style_text_font(bt, UI_FONT_M, 0);
    lv_obj_set_style_text_color(bt, lv_color_hex(COL_TXT2), 0);
    lv_label_set_text(bt, tr(S_BETA_CH));
    lv_obj_align(bt, LV_ALIGN_LEFT_MID, 16, 0);

    g_switch = lv_switch_create(g_channelbox);
    lv_obj_set_size(g_switch, 54, 28);
    lv_obj_align(g_switch, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(g_switch, lv_color_hex(0x2a2a31), LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_switch, lv_color_hex(COL_RED), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(g_switch, lv_color_hex(COL_TXT), LV_PART_KNOB);
    lv_obj_remove_flag(g_switch, LV_OBJ_FLAG_GESTURE_BUBBLE);
    if (settings_beta()) lv_obj_add_state(g_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(g_switch, beta_changed, LV_EVENT_VALUE_CHANGED, NULL);

    ota_tick();                  // 首帧立即还原空闲或后台任务真实状态,避免短暂闪错。
}

static void ota_tick(void) {
    if (!g_status) return;
    ota_status_t status = status_snapshot();
    if ((status.state == OTA_RUNNING || status.state == OTA_VERIFYING) && status.pct != s_last_pct) {
        s_last_pct = status.pct;
        orbit_progress(status.pct);
        char pb[8]; snprintf(pb, sizeof pb, "%d%%", status.pct);
        lv_label_set_text(g_pctlbl, pb);
    }
    if (status.state == s_shown && status.attempt == s_last_attempt) return;
    s_shown = status.state; s_last_attempt = status.attempt;
    stop_orbit_anim();
    bool busy = status.state == OTA_CHECKING || status.state == OTA_HEADER || status.state == OTA_RUNNING ||
                status.state == OTA_RETRYING || status.state == OTA_VERIFYING || status.state == OTA_OK;
    if (busy) {
        lv_obj_remove_flag(g_hit, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_state(g_switch, LV_STATE_DISABLED);
    } else {
        lv_obj_add_flag(g_hit, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_state(g_switch, LV_STATE_DISABLED);
    }
    switch (status.state) {
        case OTA_IDLE:     orbit_idle(); set_icon(0, COL_TXT);
                           set_visible(g_icon, true); set_visible(g_pctlbl, false);
                           lv_label_set_text(g_action, tr(S_TAP_UPDATE));
                           lv_label_set_text(g_status, ""); break;
        case OTA_HEADER:
        case OTA_CHECKING: set_icon(0, COL_TXT);
                           set_visible(g_icon, true); set_visible(g_pctlbl, false);
                           lv_label_set_text(g_action, tr(S_CHECKING));
                           lv_label_set_text(g_status, "");
                           start_orbit_anim(); break;
        case OTA_RETRYING: { set_icon(0, COL_TXT);
                           set_visible(g_icon, true); set_visible(g_pctlbl, false);
                           lv_label_set_text(g_action, tr(S_OTA_RETRY));
                           char b[40]; snprintf(b, sizeof b, "%d/%d  %d%%", status.attempt + 1,
                                                OTA_UPDATE_ATTEMPTS, status.pct);
                           lv_label_set_text(g_status, b);
                           lv_obj_set_style_text_color(g_status, lv_color_hex(COL_TXT2), 0);
                           start_orbit_anim(); } break;
        case OTA_RUNNING:  set_icon(3, 0);                            // 下载:图标让位给中心大数字
                           set_visible(g_icon, false); set_visible(g_pctlbl, true);
                           lv_label_set_text(g_action, "");
                           lv_label_set_text(g_status, tr(S_UPDATING));
                           lv_obj_set_style_text_color(g_status, lv_color_hex(COL_TXT), 0); break;
        case OTA_VERIFYING: set_visible(g_icon, false); set_visible(g_pctlbl, true);
                           lv_label_set_text(g_action, "");
                           lv_label_set_text(g_status, tr(S_OTA_VERIFY));
                           lv_obj_set_style_text_color(g_status, lv_color_hex(COL_TXT), 0); break;
        case OTA_OK:       orbit_dim(); orbit_dot_style(0, COL_CHARGE, LV_OPA_COVER);
                           set_icon(1, COL_CHARGE);
                           set_visible(g_icon, true); set_visible(g_pctlbl, false);
                           lv_label_set_text(g_action, "");
                           lv_label_set_text(g_status, tr(S_DONE_REBOOT));
                           lv_obj_set_style_text_color(g_status, lv_color_hex(COL_CHARGE), 0); break;
        case OTA_UPTODATE: { orbit_dim(); orbit_dot_style(0, COL_CHARGE, LV_OPA_COVER);
                           set_icon(1, COL_CHARGE);
                           set_visible(g_icon, true); set_visible(g_pctlbl, false);
                           lv_label_set_text(g_action, tr(S_TAP_UPDATE));
                           char b[96]; snprintf(b, sizeof b, "%s  %s", tr(S_UPTODATE), status.version);
                           lv_label_set_text(g_status, b);
                           lv_obj_set_style_text_color(g_status, lv_color_hex(COL_CHARGE), 0); } break;
        case OTA_FAIL: {   orbit_dim();
                           for (int step = 0; step < 4; step++)
                               orbit_dot_style(orbit_idx(step), COL_RED, (lv_opa_t)(255 - step * 42));
                           set_icon(2, COL_RED);
                           set_visible(g_icon, true); set_visible(g_pctlbl, false);
                           lv_label_set_text(g_action, tr(S_TAP_UPDATE));
                           str_id_t message = status.failed_at == OTA_HEADER ? S_OTA_HEADER_FAIL :
                               status.failed_at == OTA_RUNNING ? S_OTA_DOWNLOAD_FAIL :
                               status.failed_at == OTA_VERIFYING ? S_OTA_VERIFY_FAIL : S_OTA_CONNECT_FAIL;
                           if (status.tls_flags) message = S_OTA_TLS_FAIL;
                           char b[128];
                           if (status.http_status >= 400)
                               snprintf(b, sizeof b, "%s\nHTTP %d", tr(message), status.http_status);
                           else if (status.tls_code)
                               snprintf(b, sizeof b, "%s\nTLS -0x%04x", tr(message), 0u - (unsigned)status.tls_code);
                           else snprintf(b, sizeof b, "%s\n0x%04x", tr(message), (unsigned)status.error);
                           lv_label_set_text(g_status, b);
                           lv_obj_set_style_text_color(g_status, lv_color_hex(COL_RED), 0); } break;
    }
}

static void ota_exit(void) {
    stop_orbit_anim();                                   // 停动画再清指针;后台任务只发布快照
    g_status = g_ver = g_icon = g_pctlbl = g_action = NULL;
    g_orbitbox = g_hit = g_switch = g_channelbox = NULL;
    memset(g_orbit, 0, sizeof g_orbit);
}

static void ota_visibility(bool visible) {
    stop_orbit_anim();
    if (visible) {
        s_shown = (ota_state_t)-1; s_last_pct = -1;
        ota_tick();
    }
}

const app_t app_ota = { "ota", COL_TXT, ota_enter, ota_tick, ota_exit, NULL, 0, ota_visibility };
