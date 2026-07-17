// BLE HID 鼠标(HOGP)—— 见 ble_hid.h。
// GATT:HID(0x1812:HID Info / Report Map / 控制点 / 协议模式 / 输入 Report+CCCD+Report Ref)
//     + 设备信息(0x180A:PnP ID,HOGP 必需)+ 电池(0x180F:电量,顺手接 AXP2101)。
// 安全:Report/Report Map 标 READ_ENC → 主机访问即触发 Just Works 配对绑定(ble_core 配置)。
// 报文:report id 1,[buttons, dx, dy, wheel] 4 字节 notify。
#include "ble_hid.h"
#include "ble_core.h"
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
};

static bool     s_active, s_stopping, s_synced;
static uint8_t  s_addr_type;
static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_report_handle;
static volatile bool s_encrypted;        // 加密建立(= 配对完成)才算真"已连接"
static uint8_t  s_proto_mode = 1;        // 1=Report 协议(默认)

static void start_advertising(void);

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
            case 3: {   // 输入 Report 读:回空报文
                static const uint8_t zero[4] = { 0 };
                return os_mbuf_append(ctxt->om, zero, sizeof zero) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
            }
            case 4:     // 协议模式
                return os_mbuf_append(ctxt->om, &s_proto_mode, 1) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
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
        if (what == 4) {   // 协议模式写(boot/report),记下即可
            uint8_t v; uint16_t n = 0;
            if (ble_hs_mbuf_to_flat(ctxt->om, &v, 1, &n) == 0 && n) s_proto_mode = v;
        }
        return 0;          // 控制点(suspend/resume)忽略
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
              .val_handle = &s_report_handle,
              .descriptors = (struct ble_gatt_dsc_def[]) {
                  { .uuid = BLE_UUID16_DECLARE(0x2908), .att_flags = BLE_ATT_F_READ,
                    .access_cb = hid_access, .arg = (void *)2 },               // Report Reference
                  { 0 },
              } },                                                             // 输入 Report
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
            s_conn = ev->connect.conn_handle;
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
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        s_encrypted = false;
        start_advertising();
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        s_encrypted = (ev->enc_change.status == 0);
        ESP_LOGI(TAG, "encrypted=%d", s_encrypted);
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
    if (!s_active || s_stopping || !s_synced) return;

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
    s_stopping = false;
    s_active = true;
    ble_svc_gap_device_name_set(DEVICE_NAME);
    if (!ble_core_start(hid_sync)) { s_active = false; return false; }
    ESP_LOGI(TAG, "advertising as %s (HID mouse)", DEVICE_NAME);
    return true;
}

void ble_hid_stop(void) {
    s_active = false;
    s_stopping = true;
    int rc = ble_gap_adv_stop();
    if (rc != 0 && rc != BLE_HS_EALREADY) ESP_LOGW(TAG, "adv_stop rc=%d", rc);
    uint16_t conn = s_conn;
    s_conn = BLE_HS_CONN_HANDLE_NONE;
    s_encrypted = false;
    if (conn != BLE_HS_CONN_HANDLE_NONE) {
        rc = ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
        if (rc != 0 && rc != BLE_HS_ENOTCONN) ESP_LOGW(TAG, "terminate rc=%d", rc);
    }
    s_stopping = false;
    ESP_LOGI(TAG, "paused");
}

bool ble_hid_connected(void) { return s_active && s_conn != BLE_HS_CONN_HANDLE_NONE && s_encrypted; }

bool ble_hid_mouse(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel) {
    if (!ble_hid_connected()) return false;
    uint8_t rpt[4] = { buttons, (uint8_t)dx, (uint8_t)dy, (uint8_t)wheel };
    struct os_mbuf *om = ble_hs_mbuf_from_flat(rpt, sizeof rpt);
    if (!om) return false;
    return ble_gatts_notify_custom(s_conn, s_report_handle, om) == 0;
}
