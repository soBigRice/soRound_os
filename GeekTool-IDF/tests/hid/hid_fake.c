#include "hid_sdk.h"
#include "ble_core.h"
#include "power.h"
#include <stdlib.h>
#include <string.h>

extern const struct ble_gatt_svc_def BLE_HID_SVCS[];
hid_test_packet_t hid_packets[256];
int hid_packet_count, hid_notify_error, hid_terminations, hid_advertisements;
bool hid_allocate_fail, hid_start_fail;
void (*hid_notify_hook)(void);
uint16_t hid_handles[3];
static int (*callback)(struct ble_gap_event *, void *);

void hid_test_reset(void) {
    hid_packet_count = hid_notify_error = hid_terminations = hid_advertisements = 0;
    hid_allocate_fail = hid_start_fail = false; hid_notify_hook = NULL;
}
void hid_test_event(struct ble_gap_event *event) { assert(callback); callback(event, NULL); }
void hid_test_connect(uint16_t conn) {
    struct ble_gap_event e = { .type=BLE_GAP_EVENT_CONNECT, .connect={.conn_handle=conn} }; hid_test_event(&e);
}
void hid_test_encrypt(uint16_t conn) {
    struct ble_gap_event e = { .type=BLE_GAP_EVENT_ENC_CHANGE, .enc_change={.conn_handle=conn} }; hid_test_event(&e);
}
void hid_test_subscribe(uint16_t conn, int report, bool enabled) {
    struct ble_gap_event e = { .type=BLE_GAP_EVENT_SUBSCRIBE,
        .subscribe={.conn_handle=conn, .attr_handle=hid_handles[report], .cur_notify=enabled} }; hid_test_event(&e);
}
bool ble_core_start(void (*sync)(uint8_t)) {
    if (hid_start_fail) return false;
    int i = 0;
    for (const struct ble_gatt_chr_def *c=BLE_HID_SVCS[0].characteristics; c->uuid; c++) {
        if (c->val_handle) { hid_handles[i]=(uint16_t)(100+i); *c->val_handle=hid_handles[i]; i++; }
    }
    assert(i == 3); sync(0); return true;
}
bool power_read(int *soc, pwr_state_t *state) { *soc=80; *state=PWR_UNKNOWN; return true; }
int os_mbuf_append(struct os_mbuf *om, const void *data, size_t len) {
    if (om->len+len > sizeof om->data) return -1;
    memcpy(om->data+om->len,data,len); om->len+=len; return 0;
}
struct os_mbuf *ble_hs_mbuf_from_flat(const void *data, uint16_t len) {
    if (hid_allocate_fail) return NULL;
    struct os_mbuf *om=calloc(1,sizeof *om); assert(om); assert(os_mbuf_append(om,data,len)==0); return om;
}
int ble_hs_mbuf_to_flat(struct os_mbuf *om, void *data, uint16_t capacity, uint16_t *len) {
    if (om->len>capacity) return -1;
    memcpy(data,om->data,om->len); *len=(uint16_t)om->len; return 0;
}
int ble_gatts_notify_custom(uint16_t conn, uint16_t handle, struct os_mbuf *om) {
    assert(om->len<=8);
    if (!hid_notify_error) {
        assert(hid_packet_count<256);
        hid_test_packet_t *p=&hid_packets[hid_packet_count++];
        p->conn=conn; p->handle=handle; p->len=om->len; memcpy(p->bytes,om->data,om->len);
    }
    free(om);
    if (hid_notify_hook) { void (*hook)(void)=hid_notify_hook; hid_notify_hook=NULL; hook(); }
    return hid_notify_error;
}
int ble_gap_adv_start(uint8_t type, const void *peer, int32_t timeout, const struct ble_gap_adv_params *params,
                      int (*cb)(struct ble_gap_event *, void *), void *arg) {
    (void)type; (void)peer; (void)timeout; (void)params; (void)arg; callback=cb; hid_advertisements++; return 0;
}
int ble_gap_adv_set_fields(const struct ble_hs_adv_fields *f) { assert(f->uuids16[0].value==0x1812); return 0; }
int ble_gap_adv_rsp_set_fields(const struct ble_hs_adv_fields *f) { assert(f->name_len==7); return 0; }
int ble_gap_adv_stop(void) { return 0; }
int ble_gap_update_params(uint16_t conn, const struct ble_gap_upd_params *params) { (void)conn; (void)params; return 0; }
int ble_gap_terminate(uint16_t conn, uint8_t reason) { (void)conn; assert(reason==0x13); hid_terminations++; return 0; }
int ble_gap_conn_find(uint16_t conn, struct ble_gap_conn_desc *d) { d->conn_handle=conn; return 0; }
int ble_store_util_delete_peer(const int *peer) { (void)peer; return 0; }
int ble_svc_gap_device_name_set(const char *name) { assert(strcmp(name,"soRound")==0); return 0; }
