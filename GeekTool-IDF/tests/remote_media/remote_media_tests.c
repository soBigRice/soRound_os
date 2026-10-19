#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../main/remote_media.c"
#include "../../main/ble_media.c"

int64_t test_time_us=1000000;
bool test_alloc_fail, test_encrypted;
unsigned test_allocations;
int ble_gap_conn_find(uint16_t conn,struct ble_gap_conn_desc *desc) {
    if (conn!=1) return -1; desc->sec_state.encrypted=test_encrypted; return 0;
}
int os_mbuf_append(struct os_mbuf *om,const void *data,size_t size) {
    if (size>sizeof om->data-om->len) return -1;
    memcpy(om->data+om->len,data,size); om->len+=size; return 0;
}
int ble_hs_mbuf_to_flat(struct os_mbuf *om,void *dst,uint16_t capacity,uint16_t *length) {
    if (om->len>capacity) return -1; memcpy(dst,om->data,om->len); *length=(uint16_t)om->len; return 0;
}
static size_t encode(uint8_t *p,uint8_t op,uint32_t generation,uint32_t offset,uint32_t value,const void *bytes,size_t count) {
    p[0]='S'; p[1]='R'; p[2]=1; p[3]=op; write32(p+4,generation); write32(p+8,offset); write32(p+12,value);
    if (count) memcpy(p+16,bytes,count); return 16+count;
}
static bool send(uint16_t conn,uint8_t op,uint32_t gen,uint32_t offset,uint32_t value,const void *data,size_t n) {
    uint8_t p[512]; size_t count=encode(p,op,gen,offset,value,data,n); return remote_media_receive(conn,p,count);
}
static bool meta(uint16_t conn,uint32_t gen,bool playing) {
    uint8_t payload[12]={0}; write32(payload,playing ? 1 : 0); write32(payload+4,1000); write32(payload+8,10000);
    return send(conn,OP_META,gen,0,0,payload,sizeof payload);
}
static bool begin(uint32_t gen,uint32_t kind,const uint8_t *data,size_t n,bool corrupt) {
    uint8_t checksum[4]; write32(checksum,(crc_update(UINT32_MAX,data,n)^UINT32_MAX)^(corrupt ? 1 : 0));
    return send(1,OP_BEGIN,gen,kind,(uint32_t)n,checksum,4);
}
int main(void) {
    const struct ble_gatt_chr_def *rx=&BLE_MEDIA_SVCS[0].characteristics[0];
    const struct ble_gatt_chr_def *status=&BLE_MEDIA_SVCS[0].characteristics[1];
    assert((rx->flags&BLE_GATT_CHR_F_WRITE_ENC) && (status->flags&BLE_GATT_CHR_F_READ_ENC));
    assert((crc_update(UINT32_MAX,(const uint8_t *)"123456789",9)^UINT32_MAX)==0xcbf43926u);
    assert(!meta(1,1,true)); remote_media_set_visible(true); remote_media_set_link(1,false); assert(!meta(1,1,true));
    struct os_mbuf om={0}; struct ble_gatt_access_ctxt ctxt={.op=BLE_GATT_ACCESS_OP_READ_CHR,.om=&om};
    assert(status->access_cb(1,0,&ctxt,status->arg)==BLE_ATT_ERR_INSUFFICIENT_AUTHEN);
    test_encrypted=true; ble_media_hid_link(1,true); assert(!meta(2,7,true)); assert(meta(1,7,true));
    assert(status->access_cb(1,0,&ctxt,status->arg)==0 && om.len==20 && om.data[2]==1);
    uint8_t wire[512], meta_payload[12]={0}; write32(meta_payload,1); write32(meta_payload+8,10000);
    size_t wire_size=encode(wire,OP_META,7,0,0,meta_payload,12);
    om.len=wire_size; memcpy(om.data,wire,wire_size); ctxt.op=BLE_GATT_ACCESS_OP_WRITE_CHR;
    assert(rx->access_cb(1,0,&ctxt,rx->arg)==0);
    wire[2]=2; assert(!remote_media_receive(1,wire,wire_size)); wire[2]=1;
    assert(!remote_media_receive(1,wire,15));
    assert(!remote_media_receive(1,wire,REMOTE_MEDIA_PACKET_MAX+1));
    assert(meta(1,7,true));
    assert(!meta(1,6,true));
    remote_media_snapshot_t snapshot; remote_media_get_snapshot(&snapshot); assert(snapshot.connected && snapshot.playing);
    test_time_us+=2000000; remote_media_get_snapshot(&snapshot); assert(snapshot.elapsed_ms==3000);

    uint8_t *art=malloc(REMOTE_MEDIA_ARTWORK_BYTES), *mask=malloc(REMOTE_MEDIA_OVERLAY_BYTES);
    for (size_t i=0;i<REMOTE_MEDIA_ARTWORK_BYTES;++i) art[i]=(uint8_t)(i*17u);
    for (size_t i=0;i<REMOTE_MEDIA_OVERLAY_BYTES;++i) mask[i]=(uint8_t)(i*31u);
    assert(!begin(7,1,art,42,false)); test_alloc_fail=true; assert(!begin(7,1,art,REMOTE_MEDIA_ARTWORK_BYTES,false)); test_alloc_fail=false;
    assert(begin(7,1,art,REMOTE_MEDIA_ARTWORK_BYTES,false)); assert(begin(7,2,mask,REMOTE_MEDIA_OVERLAY_BYTES,false));
    assert(!send(1,OP_DATA,7,1,1,art,20)); assert(!send(1,OP_COMMIT,7,1,0,NULL,0));
    // 两图层交错并保持各自连续偏移。/ Interleave layers while preserving each layer's offsets.
    for(size_t offset=0;offset<REMOTE_MEDIA_ARTWORK_BYTES;offset+=400) {
        size_t n=REMOTE_MEDIA_ARTWORK_BYTES-offset; if(n>400) n=400;
        assert(send(1,OP_DATA,7,(uint32_t)offset,1,art+offset,n));
        if(offset<REMOTE_MEDIA_OVERLAY_BYTES) {
            size_t m=REMOTE_MEDIA_OVERLAY_BYTES-offset; if(m>400) m=400;
            assert(send(1,OP_DATA,7,(uint32_t)offset,2,mask+offset,m));
        }
    }
    assert(send(1,OP_COMMIT,7,2,0,NULL,0)); assert(send(1,OP_COMMIT,7,1,0,NULL,0));
    remote_media_get_snapshot(&snapshot); assert(snapshot.has_artwork && snapshot.has_overlay);
    uint8_t *copy=malloc(REMOTE_MEDIA_ARTWORK_BYTES);
    assert(!remote_media_copy_artwork(copy,1,snapshot.artwork_revision));
    assert(!remote_media_copy_artwork(copy,REMOTE_MEDIA_ARTWORK_BYTES,snapshot.artwork_revision-1));
    assert(remote_media_copy_artwork(copy,REMOTE_MEDIA_ARTWORK_BYTES,snapshot.artwork_revision)); assert(!memcmp(copy,art,REMOTE_MEDIA_ARTWORK_BYTES));
    assert(remote_media_copy_overlay(copy,REMOTE_MEDIA_OVERLAY_BYTES,snapshot.overlay_revision)); assert(!memcmp(copy,mask,REMOTE_MEDIA_OVERLAY_BYTES));

    assert(begin(7,2,mask,REMOTE_MEDIA_OVERLAY_BYTES,true));
    for(size_t offset=0;offset<REMOTE_MEDIA_OVERLAY_BYTES;offset+=400) {
        size_t n=REMOTE_MEDIA_OVERLAY_BYTES-offset; if(n>400) n=400;
        assert(send(1,OP_DATA,7,(uint32_t)offset,2,mask+offset,n));
    }
    assert(!send(1,OP_COMMIT,7,2,0,NULL,0));
    assert(begin(7,1,art,REMOTE_MEDIA_ARTWORK_BYTES,false)); assert(meta(1,8,false));
    assert(!send(1,OP_DATA,7,0,1,art,20)); assert(!meta(1,7,true));
    remote_media_get_snapshot(&snapshot); assert(!snapshot.has_artwork && !snapshot.has_overlay && !snapshot.playing);
    assert(begin(8,1,art,REMOTE_MEDIA_ARTWORK_BYTES,false)); test_time_us+=5000001;
    assert(!send(1,OP_DATA,8,0,1,art,20)); assert(meta(1,8,false));
    assert(begin(8,2,mask,REMOTE_MEDIA_OVERLAY_BYTES,false));
    assert(send(1,OP_CLEAR,8,0,0,NULL,0)); assert(test_allocations==0);
    test_time_us+=8000001; remote_media_tick(); remote_media_get_snapshot(&snapshot); assert(!snapshot.connected);
    assert(meta(1,9,true)); remote_media_set_link(UINT16_MAX,false); assert(!meta(1,9,true));
    remote_media_get_snapshot(&snapshot); assert(!snapshot.connected && !snapshot.has_artwork);
    remote_media_set_link(1,true); remote_media_set_visible(false); assert(!meta(1,10,true));
    assert(test_allocations==0); free(art); free(mask); free(copy);
    puts("Media: encrypted/current HID authorization, generation isolation, two-layer bounds/offsets/CRC, atomic copy, timeouts, clearing and allocation failure passed");
}
