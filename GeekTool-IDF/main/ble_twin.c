// 数字孪生 BLE 链路实现 —— 见 ble_twin.h。NimBLE 外设;host 生命周期/服务注册在 ble_core.c
//(与 HID 鼠标共享一个 NimBLE 实例,服务必须在 host 启动前一次注册齐)。
#include "ble_twin.h"
#include "ble_core.h"
#include <string.h>
#include "esp_log.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"

static const char *TAG = "ble_twin";
#define DEVICE_NAME "GeekTwin"

// 128-bit UUID 在 NimBLE 里用【小端】字节序填(= 人类可读串逐字节反转)。
// 串:c0de00XX-feed-face-cafe-0123456789ab  →  反转后如下,只有第 4 字节(00XX 里的 XX)随特征变。
static const ble_uuid128_t SVC_UUID = BLE_UUID128_INIT(
    0xab, 0x89, 0x67, 0x45, 0x23, 0x01, 0xfe, 0xca, 0xce, 0xfa, 0xed, 0xfe, 0x01, 0x00, 0xde, 0xc0);
static const ble_uuid128_t TX_UUID  = BLE_UUID128_INIT(
    0xab, 0x89, 0x67, 0x45, 0x23, 0x01, 0xfe, 0xca, 0xce, 0xfa, 0xed, 0xfe, 0x02, 0x00, 0xde, 0xc0);
static const ble_uuid128_t RX_UUID  = BLE_UUID128_INIT(
    0xab, 0x89, 0x67, 0x45, 0x23, 0x01, 0xfe, 0xca, 0xce, 0xfa, 0xed, 0xfe, 0x03, 0x00, 0xde, 0xc0);

static bool     s_running;                    // NimBLE host 是否已经初始化并运行
static volatile bool s_active;                // twin app 是否在前台:只有前台才允许广播/notify
static volatile bool s_stopping;              // 退出中:阻止 GAP 事件重新拉起广播,避免退出时又被拉活
static volatile bool s_synced;                // host sync 后才有有效本机地址类型,才能开始广播
static uint8_t  s_addr_type;
static uint16_t s_conn       = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_tx_handle;                 // TX 特征值句柄(notify 用)
static volatile bool s_notify_on;            // 中心是否已订阅 TX
static void (*s_rx_cb)(const uint8_t *, size_t);

static void start_advertising(void);

/* ---- GATT 访问回调:RX 收写入 → 转交上层;TX 读返回空 ---- */
static int gatt_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn; (void)attr; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t buf[64];
        uint16_t outlen = 0;
        if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof buf, &outlen) == 0 && s_rx_cb && outlen)
            s_rx_cb(buf, outlen);
        return 0;
    }
    return 0;   // 读 TX:无意义,返回空
}

const struct ble_gatt_svc_def BLE_TWIN_SVCS[] = {   // 由 ble_core 在 host 启动前注册
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &SVC_UUID.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {   // TX:设备 → 网页(notify)
                .uuid       = &TX_UUID.u,
                .access_cb  = gatt_access,
                .flags      = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_tx_handle,
            },
            {   // RX:网页 → 设备(write / write-no-rsp)
                .uuid      = &RX_UUID.u,
                .access_cb = gatt_access,
                .flags     = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            { 0 },
        },
    },
    { 0 },
};

/* ---- GAP 事件 ---- */
static int gap_event(struct ble_gap_event *ev, void *arg) {
    (void)arg;
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status == 0) {
            s_conn = ev->connect.conn_handle;
            ESP_LOGI(TAG, "connected");
        } else {
            start_advertising();             // 连接失败,重新广播
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected (reason=%d)", ev->disconnect.reason);
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        s_notify_on = false;
        start_advertising();                 // 正常断开后继续广播;退出中会被 start_advertising 拦住
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (ev->subscribe.attr_handle == s_tx_handle)
            s_notify_on = ev->subscribe.cur_notify;
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        return 0;
    default:
        return 0;
    }
}

/* ---- 广播:adv 包放 flags + 128bit Service UUID(供 web 按 service 过滤);名字放扫描响应避免 31B 溢出 ---- */
static void start_advertising(void) {
    if (!s_running || !s_active || s_stopping || !s_synced) return;  // app 不在前台时不广播

    struct ble_hs_adv_fields fields = { 0 };
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t[]){ SVC_UUID };
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
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

static void twin_sync(uint8_t addr_type) {   // ble_core:host 同步完成(或已同步立即回调)
    s_addr_type = addr_type;
    s_synced = true;
    start_advertising();
}

/* ---- 对外接口 ---- */
bool ble_twin_start(void) {
    s_stopping = false;
    s_active = true;
    ble_svc_gap_device_name_set(DEVICE_NAME);       // 名字随前台 app 走(与 HID 鼠标共享 host)
    if (!ble_core_start(twin_sync)) { s_active = false; return false; }
    s_running = true;
    ESP_LOGI(TAG, "advertising as %s", DEVICE_NAME);
    return true;
}

void ble_twin_stop(void) {
    if (!s_running) return;
    s_active = false;                           // 先关前台标志:后续 GAP 事件不再自动广播
    s_stopping = true;

    int rc = ble_gap_adv_stop();                // 未在广播时会返回 BLE_HS_EALREADY,属于正常退出分支
    if (rc != 0 && rc != BLE_HS_EALREADY) ESP_LOGW(TAG, "adv_stop rc=%d", rc);

    uint16_t conn = s_conn;
    s_conn      = BLE_HS_CONN_HANDLE_NONE;
    s_notify_on = false;
    if (conn != BLE_HS_CONN_HANDLE_NONE) {
        rc = ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
        if (rc != 0 && rc != BLE_HS_ENOTCONN) ESP_LOGW(TAG, "terminate rc=%d", rc);
    }

    s_stopping  = false;
    ESP_LOGI(TAG, "paused");
}

bool ble_twin_connected(void) { return s_active && s_conn != BLE_HS_CONN_HANDLE_NONE; }

bool ble_twin_notify(const uint8_t *data, size_t len) {
    if (!s_running || !s_active || s_conn == BLE_HS_CONN_HANDLE_NONE || !s_notify_on) return false;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om) return false;
    return ble_gatts_notify_custom(s_conn, s_tx_handle, om) == 0;
}

void ble_twin_set_rx_cb(void (*cb)(const uint8_t *, size_t)) { s_rx_cb = cb; }
