#pragma once
// 仅替换 NimBLE 传输/连接事件;测试编译真实 ble_hid.c 和 LVGL 页面。
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <assert.h>

#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGE(tag, ...) ((void)(tag))
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
#define BLE_HS_CONN_HANDLE_NONE UINT16_MAX
#define BLE_HS_EALREADY 2
#define BLE_HS_ENOTCONN 3
#define BLE_HS_ADV_F_DISC_GEN 2
#define BLE_HS_ADV_F_BREDR_UNSUP 4
#define BLE_HS_FOREVER INT32_MAX
#define BLE_GAP_CONN_MODE_UND 2
#define BLE_GAP_DISC_MODE_GEN 2
#define BLE_GAP_REPEAT_PAIRING_RETRY 1
#define BLE_ERR_REM_USER_CONN_TERM 0x13
#define BLE_GATT_SVC_TYPE_PRIMARY 1
#define BLE_GATT_CHR_F_READ 2
#define BLE_GATT_CHR_F_READ_ENC 0x200
#define BLE_GATT_CHR_F_NOTIFY 0x10
#define BLE_GATT_CHR_F_WRITE 8
#define BLE_GATT_CHR_F_WRITE_NO_RSP 4
#define BLE_ATT_F_READ 1
#define BLE_ATT_ERR_INSUFFICIENT_RES 0x11
#define BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN 0x0d
#define BLE_ATT_ERR_VALUE_NOT_ALLOWED 0x13
#define BLE_ATT_ERR_UNLIKELY 0x0e
#define BLE_GATT_ACCESS_OP_READ_CHR 0
#define BLE_GATT_ACCESS_OP_READ_DSC 1
#define BLE_GATT_ACCESS_OP_WRITE_CHR 2
#define BLE_GAP_EVENT_CONNECT 0
#define BLE_GAP_EVENT_DISCONNECT 1
#define BLE_GAP_EVENT_ENC_CHANGE 2
#define BLE_GAP_EVENT_REPEAT_PAIRING 3
#define BLE_GAP_EVENT_ADV_COMPLETE 4
#define BLE_GAP_EVENT_SUBSCRIBE 5
typedef struct { uint8_t type; } ble_uuid_t;
typedef struct { ble_uuid_t u; uint16_t value; } ble_uuid16_t;
#define BLE_UUID16_INIT(v) { .u = {16}, .value = (v) }
#define BLE_UUID16_DECLARE(v) ((const ble_uuid_t *)&(const ble_uuid16_t)BLE_UUID16_INIT(v))
struct os_mbuf { uint8_t data[512]; size_t len; };
struct ble_gatt_access_ctxt { int op; struct os_mbuf *om; };
typedef int (*access_fn)(uint16_t, uint16_t, struct ble_gatt_access_ctxt *, void *);
struct ble_gatt_dsc_def { const ble_uuid_t *uuid; int att_flags; access_fn access_cb; void *arg; };
struct ble_gatt_chr_def {
    const ble_uuid_t *uuid; access_fn access_cb; void *arg; int flags;
    uint16_t *val_handle; const struct ble_gatt_dsc_def *descriptors;
};
struct ble_gatt_svc_def { int type; const ble_uuid_t *uuid; const struct ble_gatt_chr_def *characteristics; };
struct ble_gap_conn_desc { uint16_t conn_handle; int peer_id_addr; };
struct ble_gap_event {
    int type;
    union {
        struct { int status; uint16_t conn_handle; } connect;
        struct { int reason; struct ble_gap_conn_desc conn; } disconnect;
        struct { int status; uint16_t conn_handle; } enc_change;
        struct { uint16_t conn_handle; } repeat_pairing;
        struct { uint16_t conn_handle, attr_handle; bool cur_notify; } subscribe;
    };
};
struct ble_gap_upd_params { int itvl_min, itvl_max, latency, supervision_timeout; };
struct ble_hs_adv_fields {
    int flags, appearance, appearance_is_present;
    ble_uuid16_t *uuids16; int num_uuids16, uuids16_is_complete;
    uint8_t *name; size_t name_len; int name_is_complete;
};
struct ble_gap_adv_params { int conn_mode, disc_mode; };
int os_mbuf_append(struct os_mbuf *, const void *, size_t);
struct os_mbuf *ble_hs_mbuf_from_flat(const void *, uint16_t);
int ble_hs_mbuf_to_flat(struct os_mbuf *, void *, uint16_t, uint16_t *);
int ble_gatts_notify_custom(uint16_t, uint16_t, struct os_mbuf *);
int ble_gap_adv_start(uint8_t, const void *, int32_t, const struct ble_gap_adv_params *, int (*)(struct ble_gap_event *, void *), void *);
int ble_gap_adv_set_fields(const struct ble_hs_adv_fields *);
int ble_gap_adv_rsp_set_fields(const struct ble_hs_adv_fields *);
int ble_gap_adv_stop(void);
int ble_gap_update_params(uint16_t, const struct ble_gap_upd_params *);
int ble_gap_terminate(uint16_t, uint8_t);
int ble_gap_conn_find(uint16_t, struct ble_gap_conn_desc *);
int ble_store_util_delete_peer(const int *);
int ble_svc_gap_device_name_set(const char *);

typedef struct { uint16_t conn, handle; size_t len; uint8_t bytes[8]; } hid_test_packet_t;
extern hid_test_packet_t hid_packets[256];
extern int hid_packet_count, hid_notify_error, hid_terminations, hid_advertisements;
extern bool hid_allocate_fail, hid_start_fail;
extern void (*hid_notify_hook)(void);
extern uint16_t hid_handles[3];
void hid_test_reset(void);
void hid_test_event(struct ble_gap_event *event);
void hid_test_connect(uint16_t conn);
void hid_test_encrypt(uint16_t conn);
void hid_test_subscribe(uint16_t conn, int report, bool enabled);
