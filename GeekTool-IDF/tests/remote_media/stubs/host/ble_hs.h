#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define BLE_GATT_SVC_TYPE_PRIMARY 1
#define BLE_GATT_CHR_F_READ 2
#define BLE_GATT_CHR_F_WRITE 8
#define BLE_GATT_CHR_F_READ_ENC 0x200
#define BLE_GATT_CHR_F_WRITE_ENC 0x1000
#define BLE_ATT_ERR_INSUFFICIENT_AUTHEN 5
#define BLE_ATT_ERR_INSUFFICIENT_RES 17
#define BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN 13
#define BLE_ATT_ERR_VALUE_NOT_ALLOWED 19
#define BLE_ATT_ERR_UNLIKELY 14
#define BLE_GATT_ACCESS_OP_READ_CHR 0
#define BLE_GATT_ACCESS_OP_WRITE_CHR 2
typedef struct { uint8_t type; } ble_uuid_t;
typedef struct { ble_uuid_t u; uint8_t value[16]; } ble_uuid128_t;
#define BLE_UUID128_INIT(...) {.u={128},.value={__VA_ARGS__}}
struct os_mbuf { uint8_t data[512]; size_t len; };
struct ble_gatt_access_ctxt { int op; struct os_mbuf *om; };
typedef int (*access_fn)(uint16_t,uint16_t,struct ble_gatt_access_ctxt *,void *);
struct ble_gatt_chr_def { const ble_uuid_t *uuid; access_fn access_cb; void *arg; int flags; };
struct ble_gatt_svc_def { int type; const ble_uuid_t *uuid; const struct ble_gatt_chr_def *characteristics; };
struct ble_gap_conn_desc { struct { bool encrypted; } sec_state; };
int ble_gap_conn_find(uint16_t,struct ble_gap_conn_desc *);
int os_mbuf_append(struct os_mbuf *,const void *,size_t);
int ble_hs_mbuf_to_flat(struct os_mbuf *,void *,uint16_t,uint16_t *);
