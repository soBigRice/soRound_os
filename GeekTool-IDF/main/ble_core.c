// NimBLE 共享核心 —— 见 ble_core.h。
#include "ble_core.h"
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ble_core";

extern const struct ble_gatt_svc_def BLE_TWIN_SVCS[];   // ble_twin.c
extern const struct ble_gatt_svc_def BLE_HID_SVCS[];    // ble_hid.c
void ble_store_config_init(void);                       // NimBLE 绑定存储(NVS_PERSIST 开启后落盘)

static bool     s_inited, s_synced;
static uint8_t  s_addr_type;
static void   (*s_sync_cb)(uint8_t);

static void on_sync(void) {
    ble_hs_id_infer_auto(0, &s_addr_type);
    s_synced = true;
    if (s_sync_cb) s_sync_cb(s_addr_type);
}
static void on_reset(int reason) { ESP_LOGW(TAG, "nimble reset, reason=%d", reason); }

static void host_task(void *param) {
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

bool ble_core_start(void (*sync_cb)(uint8_t)) {
    s_sync_cb = sync_cb;                        // 前台 app 接管 sync 后续动作
    if (s_inited) {
        if (s_synced && s_sync_cb) s_sync_cb(s_addr_type);
        return true;
    }
    if (nimble_port_init() != ESP_OK) { ESP_LOGE(TAG, "nimble_port_init failed"); return false; }

    ble_svc_gap_init();
    ble_svc_gatt_init();

    // 安全参数(HID 鼠标要求绑定加密;twin 的明文特征不受影响):
    // Just Works(无输入输出)+ 绑定 + 尽量 SC,分发加密密钥,绑定经 NVS 持久化。
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm    = 0;
    ble_hs_cfg.sm_sc      = 1;
    ble_hs_cfg.sm_io_cap  = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_our_key_dist   = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sync_cb  = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    if (ble_gatts_count_cfg(BLE_TWIN_SVCS) != 0 || ble_gatts_add_svcs(BLE_TWIN_SVCS) != 0 ||
        ble_gatts_count_cfg(BLE_HID_SVCS)  != 0 || ble_gatts_add_svcs(BLE_HID_SVCS)  != 0) {
        ESP_LOGE(TAG, "gatt register failed");
        nimble_port_deinit();
        return false;
    }
    ble_store_config_init();

    s_inited = true;
    nimble_port_freertos_init(host_task);
    return true;
}
