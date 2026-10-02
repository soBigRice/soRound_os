#include "hid_sdk.h"
#include "ble_hid.h"
#include <string.h>
#include "../../main/ble_hid.c"

int64_t host_time_us;
static void step(void) { host_time_us+=40000; ble_hid_tick(); }
static void connect_all(void) {
    ble_hid_stop(); hid_test_reset(); host_time_us+=2000000;
    assert(ble_hid_start()); hid_test_connect(1); hid_test_encrypt(1);
    for(int i=0;i<3;i++) hid_test_subscribe(1,i,true);
}
static void packet(int index, int report, size_t len, uint16_t usage) {
    const hid_test_packet_t *p=&hid_packets[index];
    assert(p->handle==hid_handles[report] && p->len==len && p->conn==1);
    if (report==HID_KEYBOARD) { assert(p->bytes[2]==usage); assert(p->bytes[0]==0 && p->bytes[1]==0); }
    else if (report==HID_MEDIA) assert((p->bytes[0] | p->bytes[1]<<8)==usage);
    else assert(p->bytes[0]==usage);
}
static void descriptor_contract(void) {
    unsigned bits[4]={0}, id=0, size=0, count=0;
    for(size_t p=0;p<sizeof REPORT_MAP;) {
        uint8_t item=REPORT_MAP[p++]; unsigned len=item&3; if(len==3) len=4;
        unsigned value=0; assert(p+len<=sizeof REPORT_MAP);
        for(unsigned i=0;i<len;i++) value|=(unsigned)REPORT_MAP[p++]<<(8*i);
        switch(item&0xfc) {
        case 0x84: id=value; assert(id>0 && id<4); break;
        case 0x74: size=value; break;
        case 0x94: count=value; break;
        case 0x80: bits[id]+=size*count; break;
        }
    }
    assert(bits[1]==32 && bits[2]==64 && bits[3]==16);
    int reports=0;
    for(const struct ble_gatt_chr_def *c=BLE_HID_SVCS[0].characteristics;c->uuid;c++) if(c->val_handle) {
        struct os_mbuf om={0}; struct ble_gatt_access_ctxt ctx={.op=BLE_GATT_ACCESS_OP_READ_DSC,.om=&om};
        const struct ble_gatt_dsc_def *d=c->descriptors;
        assert(d->access_cb(1,0,&ctx,d->arg)==0); assert(om.len==2 && om.data[0]==reports+1 && om.data[1]==1);
        om.len=0; ctx.op=BLE_GATT_ACCESS_OP_READ_CHR;
        assert(c->access_cb(1,0,&ctx,c->arg)==0);
        const size_t lengths[]={4,8,2}; assert(om.len==lengths[reports++]);
    }
    assert(reports==3);
}
static void reconnect_during_send(void) {
    struct ble_gap_event e={.type=BLE_GAP_EVENT_DISCONNECT,.disconnect={.conn={.conn_handle=1}}};
    hid_test_event(&e); hid_test_connect(2); hid_test_encrypt(2);
    for(int i=0;i<3;i++) hid_test_subscribe(2,i,true);
}
int main(void) {
    descriptor_contract();
    connect_all(); hid_test_connect(1);
    assert(!ble_hid_connected() && !ble_hid_key_tap(HID_KEY_PAGE_DOWN));
    hid_test_encrypt(9); assert(!ble_hid_connected());
    hid_test_encrypt(1); assert(ble_hid_connected() && !ble_hid_ready(HID_MOUSE));
    hid_test_subscribe(9,HID_MOUSE,true); assert(!ble_hid_ready(HID_MOUSE));
    hid_test_subscribe(1,HID_MOUSE,true); assert(ble_hid_ready(HID_MOUSE));
    assert(!ble_hid_key_tap(HID_KEY_PAGE_DOWN));
    assert(ble_hid_mouse(3,-12,18,-2)); packet(0,HID_MOUSE,4,3);
    assert(hid_packets[0].bytes[1]==(uint8_t)-12 && hid_packets[0].bytes[2]==18 && hid_packets[0].bytes[3]==(uint8_t)-2);
    assert(!ble_hid_mouse(8,0,0,0));

    connect_all();
    assert(ble_hid_key_tap(HID_KEY_PAGE_DOWN)); assert(ble_hid_key_tap(HID_KEY_PAGE_DOWN));
    step(); packet(0,HID_KEYBOARD,8,0x4e);
    ble_hid_tick(); assert(hid_packet_count==1); // 同一帧不得把按下/释放挤成一个动作。
    step(); packet(1,HID_KEYBOARD,8,0); step(); packet(2,HID_KEYBOARD,8,0x4e); step(); packet(3,HID_KEYBOARD,8,0);
    const uint16_t usages[]={0xb5,0xb6,0xcd,0xe2,0xe9,0xea};
    for(unsigned i=0;i<6;i++) { assert(ble_hid_media_tap(usages[i])); step(); packet(4+i*2,HID_MEDIA,2,usages[i]); step(); packet(5+i*2,HID_MEDIA,2,0); }
    assert(!ble_hid_media_tap(0xffff) && !ble_hid_key_tap(0));

    connect_all();
    for(int i=0;i<8;i++) assert(ble_hid_key_tap(HID_KEY_PAGE_UP));
    assert(!ble_hid_key_tap(HID_KEY_PAGE_UP));
    for(int i=0;i<16;i++) step(); assert(hid_packet_count==16);
    assert(ble_hid_key_tap(HID_KEY_PAGE_DOWN)); ble_hid_release_all(); step(); assert(hid_packet_count==16);
    assert(ble_hid_media_tap(HID_MEDIA_PLAY_PAUSE)); step(); ble_hid_release_all(); step(); packet(17,HID_MEDIA,2,0);
    assert(hid_packet_count==18);

    connect_all();
    assert(ble_hid_key_tap(HID_KEY_PAGE_UP)); step();
    hid_notify_error=1; step(); assert(hid_packet_count==1);
    hid_notify_error=0; step(); packet(1,HID_KEYBOARD,8,0);
    assert(ble_hid_mouse(1,0,0,0)); hid_notify_error=1;
    assert(!ble_hid_mouse(0,0,0,0) && !ble_hid_ready(HID_MOUSE));
    hid_notify_error=0; step(); packet(3,HID_MOUSE,4,0); assert(ble_hid_ready(HID_MOUSE));

    connect_all();
    assert(ble_hid_mouse(3,0,0,0)); hid_notify_error=1;
    // 同时按住左右键后松开一个,发送失败也必须释放主机已持有的输入。
    assert(!ble_hid_mouse(2,0,0,0) && !ble_hid_ready(HID_MOUSE));
    hid_notify_error=0; step(); packet(1,HID_MOUSE,4,0); assert(ble_hid_ready(HID_MOUSE));

    connect_all(); hid_allocate_fail=true;
    assert(ble_hid_key_tap(HID_KEY_PAGE_DOWN)); step(); assert(hid_packet_count==0);
    hid_allocate_fail=false; step(); packet(0,HID_KEYBOARD,8,0x4e);
    hid_notify_error=1; for(int i=0;i<28;i++) step();
    assert(hid_terminations==1 && !ble_hid_connected());
    hid_notify_error=0; hid_test_connect(1); hid_test_encrypt(1); hid_test_subscribe(1,HID_KEYBOARD,true);
    step(); assert(hid_packet_count==1); // 断连不重放旧动作。

    connect_all(); hid_notify_hook=ble_hid_release_all;
    assert(ble_hid_key_tap(HID_KEY_PAGE_UP)); step(); step(); packet(1,HID_KEYBOARD,8,0);
    step(); assert(hid_packet_count==2 && s_count==0);
    connect_all(); assert(ble_hid_key_tap(HID_KEY_PAGE_DOWN)); step();
    hid_test_subscribe(1,HID_KEYBOARD,false);
    for(int i=0;i<28;i++) step(); assert(hid_terminations==1 && !ble_hid_connected());
    connect_all(); hid_notify_hook=reconnect_during_send;
    assert(ble_hid_key_tap(HID_KEY_PAGE_DOWN)); step(); step(); assert(hid_packet_count==1 && s_count==0);

    connect_all();
    struct os_mbuf om={.data={0},.len=1}; struct ble_gatt_access_ctxt ctx={.op=BLE_GATT_ACCESS_OP_WRITE_CHR,.om=&om};
    assert(hid_access(1,0,&ctx,(void *)4)==BLE_ATT_ERR_VALUE_NOT_ALLOWED);
    assert(hid_access(1,0,&ctx,(void *)7)==0 && !ble_hid_ready(HID_MOUSE));
    om.data[0]=1; assert(hid_access(1,0,&ctx,(void *)7)==0 && ble_hid_ready(HID_MOUSE));
    assert(ble_hid_key_tap(HID_KEY_PAGE_DOWN)); hid_test_subscribe(1,HID_KEYBOARD,false); step(); assert(hid_packet_count==0);
    ble_hid_stop(); step(); assert(!ble_hid_connected());
    puts("HID: report contracts, notification readiness, signed mouse, keyboard/media taps, queue bounds, releases, failure recovery and connection races passed");
}
