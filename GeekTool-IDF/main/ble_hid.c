// BLE HID 遥控台(HOGP)—— 见 ble_hid.h。
// GATT:HID(0x1812:HID Info / Report Map / 控制点 / 协议模式 / 输入 Report+CCCD+Report Ref)
//     + 设备信息(0x180A:PnP ID,HOGP 必需)+ 电池(0x180F:电量,顺手接 AXP2101)。
// 安全:Report/Report Map 标 READ_ENC → 主机访问即触发 Just Works 配对绑定(ble_core 配置)。
// 报文:ID1 鼠标 4 字节、ID2 键盘 8 字节、ID3 媒体 2 字节 notify。
#include "ble_hid.h"
#include "ble_core.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "power.h"
#include <string.h>
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "services/gap/ble_svc_gap.h"

static const char *TAG = "ble_hid";
#define DEVICE_NAME  "soRound"
#define APPEARANCE_MOUSE 0x03C2          // GAP 外观:HID 鼠标(主机据此显示鼠标图标)

/* HID 报告描述符:标准 3 键 + XY 相对位移 + 滚轮,report id 1 */
static const uint8_t REPORT_MAP[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Buttons)
    0x19, 0x01, 0x29, 0x03,   // 按键 1-3
    0x15, 0x00, 0x25, 0x01,   // 0/1
    0x95, 0x03, 0x75, 0x01,   // 3 个 1bit
    0x81, 0x02,        //     Input (Data,Var,Abs)
    0x95, 0x01, 0x75, 0x05,   // 5bit 填充
    0x81, 0x03,        //     Input (Const)
    0x05, 0x01,        //     Usage Page (Generic Desktop)
    0x09, 0x30, 0x09, 0x31, 0x09, 0x38,   // X, Y, Wheel
    0x15, 0x81, 0x25, 0x7F,   // -127..127
    0x75, 0x08, 0x95, 0x03,   // 3 个 8bit
    0x81, 0x06,        //     Input (Data,Var,Rel)
    0xC0,              //   End Collection
    0xC0,              // End Collection
    // 键盘 ID2:标准 8 字节(修饰键、保留、6 个键),Report 特征负载不带 ID。
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x02,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x75, 0x08, 0x95, 0x01, 0x81, 0x03,
    0x19, 0x00, 0x29, 0x65, 0x15, 0x00, 0x25, 0x65,
    0x75, 0x08, 0x95, 0x06, 0x81, 0x00, 0xC0,
    // 媒体 ID3:一个 16 位 Consumer Usage 数组项,0 表示释放。
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x03,
    0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03,
    0x75, 0x10, 0x95, 0x01, 0x81, 0x00, 0xC0,
};

static bool     s_active, s_stopping, s_synced;
static uint8_t  s_addr_type;
static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_report_handles[HID_REPORT_COUNT];
static volatile bool s_encrypted;        // 加密建立(= 配对完成)才算真"已连接"
static bool s_subscribed[HID_REPORT_COUNT], s_suspended;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_generation, s_queue_generation;
static uint8_t s_mouse_buttons, s_key_down, s_release_mask;
static uint16_t s_media_down;
// UI 线程入队/pump;GAP 线程只更新连接/订阅并清空旧队列。临界区不做 BLE I/O。
#define TAP_QUEUE_SIZE 8
#define TAP_INTERVAL_US 35000
#define SEND_TIMEOUT_US 1000000
typedef struct { hid_report_t report; uint16_t usage; } hid_tap_t;
static hid_tap_t s_taps[TAP_QUEUE_SIZE];
static uint8_t s_head, s_count;
static bool s_press_sent;
static int64_t s_next_send_us, s_failed_at_us;

static void start_advertising(void);

static void reset_input_locked(void) {
    s_generation++;
    memset(s_subscribed, 0, sizeof s_subscribed);
    s_suspended = false;
    s_mouse_buttons = s_key_down = s_release_mask = 0;
    s_media_down = 0;
    s_head = s_count = 0;
    s_press_sent = false;
    s_next_send_us = s_failed_at_us = 0;
}

static void cancel_taps_locked(void) {
    s_queue_generation++;
    s_head = s_count = 0;
    s_press_sent = false;
    if (s_mouse_buttons) s_release_mask |= 1u << HID_MOUSE;
    if (s_key_down) s_release_mask |= 1u << HID_KEYBOARD;
    if (s_media_down) s_release_mask |= 1u << HID_MEDIA;
    if (!s_release_mask) s_failed_at_us = 0;
    s_next_send_us = 0;
}

static bool ready_locked(hid_report_t report) {
    return s_active && s_conn != BLE_HS_CONN_HANDLE_NONE && s_encrypted &&
           !s_suspended && s_subscribed[report];
}

static size_t make_report(hid_report_t report, uint16_t usage, uint8_t bytes[8]) {
    memset(bytes, 0, 8);
    if (report == HID_MOUSE) { bytes[0] = (uint8_t)usage; return 4; }
    if (report == HID_KEYBOARD) { bytes[2] = (uint8_t)usage; return 8; }
    bytes[0] = usage & 0xff; bytes[1] = usage >> 8; return 2;
}

/* ---- GATT 访问回调 ---- */
static int hid_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn; (void)attr;
    int what = (int)(intptr_t)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR || ctxt->op == BLE_GATT_ACCESS_OP_READ_DSC) {
        switch (what) {
            case 0: {   // HID Information:bcdHID 1.11,国家码 0,flags=normally connectable
                static const uint8_t info[4] = { 0x11, 0x01, 0x00, 0x02 };
                return os_mbuf_append(ctxt->om, info, sizeof info) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
            }
            case 1:     // Report Map
                return os_mbuf_append(ctxt->om, REPORT_MAP, sizeof REPORT_MAP) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
            case 2: {   // Report Reference 描述符:report id 1,类型 input(1)
                static const uint8_t ref[2] = { 0x01, 0x01 };
                return os_mbuf_append(ctxt->om, ref, sizeof ref) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
            }
            case 3: case 10: case 11: {
                hid_report_t report = what == 3 ? HID_MOUSE : what == 10 ? HID_KEYBOARD : HID_MEDIA;
                uint8_t bytes[8];
                portENTER_CRITICAL(&s_mux);
                uint16_t usage = report == HID_MOUSE ? s_mouse_buttons : report == HID_KEYBOARD ? s_key_down : s_media_down;
                portEXIT_CRITICAL(&s_mux);
                // 相对位移在读取时归零,按住状态仍准确返回。
                size_t len = make_report(report, usage, bytes);
                return os_mbuf_append(ctxt->om, bytes, len) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
            }
            case 4: {
                const uint8_t mode = 1; // 只实现 Report Protocol,不伪装支持 Boot Protocol。
                return os_mbuf_append(ctxt->om, &mode, 1) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
            }
            case 8: case 9: {
                const uint8_t ref[2] = { what == 8 ? 2 : 3, 1 };
                return os_mbuf_append(ctxt->om, ref, sizeof ref) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
            }
            case 5: {   // PnP ID:USB 源,Espressif VID 0x303A,PID 0x0001,版本 1.0
                static const uint8_t pnp[7] = { 0x02, 0x3A, 0x30, 0x01, 0x00, 0x00, 0x01 };
                return os_mbuf_append(ctxt->om, pnp, sizeof pnp) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
            }
            case 6: {   // 电池电量
                int soc = 100; pwr_state_t st;
                power_read(&soc, &st);
                uint8_t lvl = (uint8_t)(soc < 0 ? 0 : soc > 100 ? 100 : soc);
                return os_mbuf_append(ctxt->om, &lvl, 1) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
            }
        }
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (what == 4 || what == 7) {
            uint8_t v; uint16_t n = 0;
            if (ble_hs_mbuf_to_flat(ctxt->om, &v, 1, &n) != 0 || n != 1)
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            if (v > 1 || (what == 4 && v != 1)) return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
            if (what == 7) {
                portENTER_CRITICAL(&s_mux);
                s_suspended = v == 0;
                cancel_taps_locked();
                portEXIT_CRITICAL(&s_mux);
            }
        }
        return 0;
    }
    return 0;
}

const struct ble_gatt_svc_def BLE_HID_SVCS[] = {   // 由 ble_core 在 host 启动前注册
    {   // HID 服务
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1812),
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = BLE_UUID16_DECLARE(0x2A4A), .access_cb = hid_access, .arg = (void *)0,
              .flags = BLE_GATT_CHR_F_READ },                                  // HID Info
            { .uuid = BLE_UUID16_DECLARE(0x2A4B), .access_cb = hid_access, .arg = (void *)1,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC },        // Report Map(读需加密→触发配对)
            { .uuid = BLE_UUID16_DECLARE(0x2A4C), .access_cb = hid_access, .arg = (void *)7,
              .flags = BLE_GATT_CHR_F_WRITE_NO_RSP },                          // HID Control Point
            { .uuid = BLE_UUID16_DECLARE(0x2A4E), .access_cb = hid_access, .arg = (void *)4,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP },    // Protocol Mode
            { .uuid = BLE_UUID16_DECLARE(0x2A4D), .access_cb = hid_access, .arg = (void *)3,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &s_report_handles[HID_MOUSE],
              .descriptors = (struct ble_gatt_dsc_def[]) {
                  { .uuid = BLE_UUID16_DECLARE(0x2908), .att_flags = BLE_ATT_F_READ,
                    .access_cb = hid_access, .arg = (void *)2 },               // Report Reference
                  { 0 },
              } },                                                             // 输入 Report
            { .uuid = BLE_UUID16_DECLARE(0x2A4D), .access_cb = hid_access, .arg = (void *)10,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &s_report_handles[HID_KEYBOARD],
              .descriptors = (struct ble_gatt_dsc_def[]) {
                  { .uuid = BLE_UUID16_DECLARE(0x2908), .att_flags = BLE_ATT_F_READ,
                    .access_cb = hid_access, .arg = (void *)8 }, { 0 },
              } },
            { .uuid = BLE_UUID16_DECLARE(0x2A4D), .access_cb = hid_access, .arg = (void *)11,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &s_report_handles[HID_MEDIA],
              .descriptors = (struct ble_gatt_dsc_def[]) {
                  { .uuid = BLE_UUID16_DECLARE(0x2908), .att_flags = BLE_ATT_F_READ,
                    .access_cb = hid_access, .arg = (void *)9 }, { 0 },
              } },
            { 0 },
        },
    },
    {   // 设备信息(PnP ID 为 HOGP 必需)
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180A),
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = BLE_UUID16_DECLARE(0x2A50), .access_cb = hid_access, .arg = (void *)5,
              .flags = BLE_GATT_CHR_F_READ },
            { 0 },
        },
    },
    {   // 电池
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180F),
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = BLE_UUID16_DECLARE(0x2A19), .access_cb = hid_access, .arg = (void *)6,
              .flags = BLE_GATT_CHR_F_READ },
            { 0 },
        },
    },
    { 0 },
};

/* ---- GAP ---- */
static int gap_event(struct ble_gap_event *ev, void *arg) {
    (void)arg;
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status == 0) {
            portENTER_CRITICAL(&s_mux);
            reset_input_locked();
            s_conn = ev->connect.conn_handle;
            s_encrypted = false;
            portEXIT_CRITICAL(&s_mux);
            // HID 要低延迟连接间隔(7.5-15ms);多数主机会接受
            struct ble_gap_upd_params up = { .itvl_min = 6, .itvl_max = 12, .latency = 0,
                                             .supervision_timeout = 200 };
            ble_gap_update_params(s_conn, &up);
            ESP_LOGI(TAG, "connected");
        } else {
            start_advertising();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected (reason=%d)", ev->disconnect.reason);
        portENTER_CRITICAL(&s_mux);
        if (ev->disconnect.conn.conn_handle == s_conn) {
            s_conn = BLE_HS_CONN_HANDLE_NONE;
            s_encrypted = false;
            reset_input_locked();
        }
        portEXIT_CRITICAL(&s_mux);
        start_advertising();
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        portENTER_CRITICAL(&s_mux);
        if (ev->enc_change.conn_handle == s_conn) s_encrypted = (ev->enc_change.status == 0);
        if (!s_encrypted) cancel_taps_locked();
        portEXIT_CRITICAL(&s_mux);
        ESP_LOGI(TAG, "encrypted=%d", s_encrypted);
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
        portENTER_CRITICAL(&s_mux);
        if (ev->subscribe.conn_handle == s_conn) {
            for (int i = 0; i < HID_REPORT_COUNT; i++) if (ev->subscribe.attr_handle == s_report_handles[i]) {
                s_subscribed[i] = ev->subscribe.cur_notify;
                if (!s_subscribed[i]) cancel_taps_locked();
            }
        }
        portEXIT_CRITICAL(&s_mux);
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        // 主机忘了旧绑定又来配:删掉我们这边的旧绑定,允许重配(不然永远配不上)
        struct ble_gap_conn_desc d;
        if (ble_gap_conn_find(ev->repeat_pairing.conn_handle, &d) == 0)
            ble_store_util_delete_peer(&d.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        return 0;
    default:
        return 0;
    }
}

/* ---- 广播:flags + 外观(鼠标)+ HID 服务 UUID;名字放扫描响应 ---- */
static void start_advertising(void) {
    if (!s_active || s_stopping || !s_synced || s_conn != BLE_HS_CONN_HANDLE_NONE) return;

    struct ble_hs_adv_fields fields = { 0 };
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.appearance = APPEARANCE_MOUSE;
    fields.appearance_is_present = 1;
    fields.uuids16 = (ble_uuid16_t[]){ BLE_UUID16_INIT(0x1812) };
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
    if (ble_gap_adv_set_fields(&fields) != 0) { ESP_LOGE(TAG, "adv_set_fields failed"); return; }

    struct ble_hs_adv_fields rsp = { 0 };
    rsp.name = (uint8_t *)DEVICE_NAME;
    rsp.name_len = strlen(DEVICE_NAME);
    rsp.name_is_complete = 1;
    ble_gap_adv_rsp_set_fields(&rsp);

    struct ble_gap_adv_params adv = { 0 };
    adv.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv.disc_mode = BLE_GAP_DISC_MODE_GEN;
    int rc = ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &adv, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) ESP_LOGE(TAG, "adv_start rc=%d", rc);
}

static void hid_sync(uint8_t addr_type) {
    s_addr_type = addr_type;
    s_synced = true;
    start_advertising();
}

/* ---- 对外接口 ---- */
bool ble_hid_start(void) {
    portENTER_CRITICAL(&s_mux);
    s_stopping = false;
    s_active = true;
    portEXIT_CRITICAL(&s_mux);
    ble_svc_gap_device_name_set(DEVICE_NAME);
    if (!ble_core_start(hid_sync)) { s_active = false; return false; }
    ESP_LOGI(TAG, "advertising as %s (mouse / slides / media)", DEVICE_NAME);
    return true;
}

void ble_hid_stop(void) {
    portENTER_CRITICAL(&s_mux);
    s_active = false;
    s_stopping = true;
    uint16_t conn = s_conn;
    s_conn = BLE_HS_CONN_HANDLE_NONE;
    s_encrypted = false;
    reset_input_locked();
    portEXIT_CRITICAL(&s_mux);
    int rc = ble_gap_adv_stop();
    if (rc != 0 && rc != BLE_HS_EALREADY) ESP_LOGW(TAG, "adv_stop rc=%d", rc);
    if (conn != BLE_HS_CONN_HANDLE_NONE) {
        rc = ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
        if (rc != 0 && rc != BLE_HS_ENOTCONN) ESP_LOGW(TAG, "terminate rc=%d", rc);
    }
    s_stopping = false;
    ESP_LOGI(TAG, "paused");
}

bool ble_hid_connected(void) {
    portENTER_CRITICAL(&s_mux);
    bool connected = s_active && s_conn != BLE_HS_CONN_HANDLE_NONE && s_encrypted;
    portEXIT_CRITICAL(&s_mux);
    return connected;
}

bool ble_hid_ready(hid_report_t report) {
    if (report < 0 || report >= HID_REPORT_COUNT) return false;
    portENTER_CRITICAL(&s_mux);
    bool ready = ready_locked(report) && !(s_release_mask & (1u << report));
    portEXIT_CRITICAL(&s_mux);
    return ready;
}

static bool send_report(uint16_t conn, hid_report_t report, const uint8_t *bytes, size_t len) {
    struct os_mbuf *om = ble_hs_mbuf_from_flat(bytes, len);
    return om && ble_gatts_notify_custom(conn, s_report_handles[report], om) == 0;
}

bool ble_hid_mouse(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel) {
    if (buttons & ~0x07) return false;
    portENTER_CRITICAL(&s_mux);
    bool ready = ready_locked(HID_MOUSE) && !(s_release_mask & (1u << HID_MOUSE));
    uint16_t conn = s_conn; uint32_t generation = s_generation;
    uint32_t queue_generation = s_queue_generation;
    portEXIT_CRITICAL(&s_mux);
    if (!ready) return false;
    uint8_t rpt[4] = { buttons, (uint8_t)dx, (uint8_t)dy, (uint8_t)wheel };
    bool sent = send_report(conn, HID_MOUSE, rpt, sizeof rpt);
    portENTER_CRITICAL(&s_mux);
    if (generation == s_generation) {
        if (sent) {
            s_mouse_buttons = buttons;
            if (queue_generation != s_queue_generation && buttons) s_release_mask |= 1u << HID_MOUSE;
        }
        // 任意按键释放失败都进入零状态恢复,包括左右键同时按住后只松开其中一个。
        else if (s_mouse_buttons & ~buttons) s_release_mask |= 1u << HID_MOUSE;
    }
    portEXIT_CRITICAL(&s_mux);
    return sent;
}

static bool enqueue_tap(hid_report_t report, uint16_t usage) {
    portENTER_CRITICAL(&s_mux);
    bool ready = ready_locked(report) && !s_release_mask && s_count < TAP_QUEUE_SIZE;
    if (ready) {
        s_taps[(s_head + s_count) % TAP_QUEUE_SIZE] = (hid_tap_t){report, usage};
        s_count++;
    }
    portEXIT_CRITICAL(&s_mux);
    return ready;
}

bool ble_hid_key_tap(uint8_t key) {
    return (key == HID_KEY_PAGE_UP || key == HID_KEY_PAGE_DOWN) && enqueue_tap(HID_KEYBOARD, key);
}

bool ble_hid_media_tap(uint16_t usage) {
    switch (usage) {
    case HID_MEDIA_NEXT: case HID_MEDIA_PREVIOUS: case HID_MEDIA_PLAY_PAUSE:
    case HID_MEDIA_MUTE: case HID_MEDIA_VOLUME_UP: case HID_MEDIA_VOLUME_DOWN:
        return enqueue_tap(HID_MEDIA, usage);
    default: return false;
    }
}

void ble_hid_release_all(void) {
    portENTER_CRITICAL(&s_mux);
    cancel_taps_locked();
    portEXIT_CRITICAL(&s_mux);
}

void ble_hid_tick(void) {
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_mux);
    bool releasing = s_release_mask != 0;
    if ((!releasing && !s_count) || now < s_next_send_us) { portEXIT_CRITICAL(&s_mux); return; }
    hid_report_t report = releasing ? HID_MOUSE : s_taps[s_head].report;
    if (releasing) while (!(s_release_mask & (1u << report))) report++;
    if (!ready_locked(report)) {
        // 主机撤销通知后也不能永久卡在释放阶段。Suspend 由主机管理,恢复后再释放。
        uint16_t disconnect = BLE_HS_CONN_HANDLE_NONE;
        if (releasing && !s_suspended && s_conn != BLE_HS_CONN_HANDLE_NONE) {
            if (!s_failed_at_us) s_failed_at_us = now ? now : 1;
            if (now - s_failed_at_us >= SEND_TIMEOUT_US) {
                disconnect = s_conn; s_conn = BLE_HS_CONN_HANDLE_NONE; s_encrypted = false;
                reset_input_locked();
            }
        }
        portEXIT_CRITICAL(&s_mux);
        if (disconnect != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(disconnect, BLE_ERR_REM_USER_CONN_TERM);
        return;
    }
    uint16_t usage = releasing || s_press_sent ? 0 : s_taps[s_head].usage;
    uint16_t conn = s_conn; uint32_t generation = s_generation;
    uint32_t queue_generation = s_queue_generation;
    portEXIT_CRITICAL(&s_mux);
    uint8_t bytes[8]; size_t len = make_report(report, usage, bytes);
    bool sent = send_report(conn, report, bytes, len);
    bool timed_out = false;
    portENTER_CRITICAL(&s_mux);
    if (generation == s_generation) {
        s_next_send_us = now + TAP_INTERVAL_US;
        if (sent) {
            s_failed_at_us = 0;
            if (report == HID_MOUSE) s_mouse_buttons = (uint8_t)usage;
            else if (report == HID_KEYBOARD) s_key_down = (uint8_t)usage;
            else s_media_down = usage;
            // GAP 取消可能发生在 notify 期间。记录已提交给 NimBLE 的状态,不能推进已清空的队列。
            if (queue_generation != s_queue_generation) {
                if (usage) s_release_mask |= 1u << report;
                else s_release_mask &= ~(1u << report);
            } else if (releasing) s_release_mask &= ~(1u << report);
            else if (s_press_sent) { s_press_sent = false; s_head = (s_head + 1) % TAP_QUEUE_SIZE; s_count--; }
            else s_press_sent = true;
        } else {
            if (!s_failed_at_us) s_failed_at_us = now ? now : 1;
            if (now - s_failed_at_us >= SEND_TIMEOUT_US) {
                timed_out = true;
                s_conn = BLE_HS_CONN_HANDLE_NONE;
                s_encrypted = false;
                reset_input_locked();
            }
        }
    }
    portEXIT_CRITICAL(&s_mux);
    // 无法交付释放报文时断开,让主机清除按住状态;不把旧动作带进下一次连接。
    if (timed_out) {
        ESP_LOGW(TAG, "report send timed out; disconnect to release input");
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    }
}
