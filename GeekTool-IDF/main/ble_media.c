#include "ble_media.h"
#include "remote_media.h"
#include "host/ble_hs.h"

// UUID:73520001/2/3-6e93-4fd8-bf40-2b54f4aa4f47；NimBLE 按小端存储。
// UUIDs use NimBLE's little-endian representation.
static const ble_uuid128_t SERVICE=BLE_UUID128_INIT(0x47,0x4f,0xaa,0xf4,0x54,0x2b,0x40,0xbf,0xd8,0x4f,0x93,0x6e,0x01,0x00,0x52,0x73);
static const ble_uuid128_t RX=BLE_UUID128_INIT(0x47,0x4f,0xaa,0xf4,0x54,0x2b,0x40,0xbf,0xd8,0x4f,0x93,0x6e,0x02,0x00,0x52,0x73);
static const ble_uuid128_t STATUS=BLE_UUID128_INIT(0x47,0x4f,0xaa,0xf4,0x54,0x2b,0x40,0xbf,0xd8,0x4f,0x93,0x6e,0x03,0x00,0x52,0x73);

void ble_media_hid_link(uint16_t connection, bool encrypted) {
    remote_media_set_link(connection,encrypted);
}
static int access_media(uint16_t connection, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)attr;
    // 两重检查：GATT 加密属性＋实际当前 HID 会话。/ Check GATT encryption and current HID session.
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(connection,&desc)!=0 || !desc.sec_state.encrypted)
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    if (arg && ctxt->op==BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t status[20];
        if (!remote_media_status(connection,status)) return BLE_ATT_ERR_UNLIKELY;
        return os_mbuf_append(ctxt->om,status,sizeof status) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
    }
    if (!arg && ctxt->op==BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t packet[REMOTE_MEDIA_PACKET_MAX]; uint16_t length=0;
        if (ble_hs_mbuf_to_flat(ctxt->om,packet,sizeof packet,&length)!=0)
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        return remote_media_receive(connection,packet,length) ? 0 : BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }
    return BLE_ATT_ERR_UNLIKELY;
}
const struct ble_gatt_svc_def BLE_MEDIA_SVCS[]={
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&SERVICE.u,
     .characteristics=(struct ble_gatt_chr_def[]) {
         {.uuid=&RX.u,.access_cb=access_media,.flags=BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_WRITE_ENC},
         {.uuid=&STATUS.u,.access_cb=access_media,.arg=(void *)1,.flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_READ_ENC},
         {0},
     }},
    {0},
};
