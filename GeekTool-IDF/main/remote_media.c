#include "remote_media.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

#define NO_CONNECTION UINT16_MAX
#define TRANSFER_IDLE_US 5000000LL
#define TRANSFER_TOTAL_US 120000000LL
#define BRIDGE_IDLE_US 8000000LL
enum { OP_META=1, OP_BEGIN, OP_DATA, OP_COMMIT, OP_CLEAR, OP_RESET };
enum { LAYER_ARTWORK=1, LAYER_OVERLAY=2 };

static SemaphoreHandle_t s_lock;
static bool s_visible, s_encrypted;
static uint16_t s_connection = NO_CONNECTION;
static remote_media_snapshot_t s_state;
static uint8_t *s_artwork, *s_overlay;
typedef struct {
    uint8_t *bytes;
    uint32_t size, offset, expected_crc, crc;
    int64_t started_us, chunk_us;
} transfer_t;
static transfer_t s_transfers[2];
static int64_t s_meta_us;

static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void write32(uint8_t *p, uint32_t n) {
    for (unsigned i=0; i<4; ++i) p[i]=(uint8_t)(n>>(8*i));
}
static uint32_t crc_update(uint32_t crc, const uint8_t *p, size_t n) {
    while (n--) {
        crc ^= *p++;
        for (unsigned bit=0; bit<8; ++bit) crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);
    }
    return crc;
}
static void cancel_transfer_locked(transfer_t *t) {
    heap_caps_free(t->bytes); memset(t,0,sizeof *t);
}
static void clear_layer_locked(uint32_t kind) {
    if (kind==0 || kind==LAYER_ARTWORK) {
        heap_caps_free(s_artwork); s_artwork=NULL;
        s_state.has_artwork=false; ++s_state.artwork_revision;
    }
    if (kind==0 || kind==LAYER_OVERLAY) {
        heap_caps_free(s_overlay); s_overlay=NULL;
        s_state.has_overlay=false; ++s_state.overlay_revision;
    }
    ++s_state.revision;
}
static void reset_locked(void) {
    for (unsigned i=0; i<2; ++i) cancel_transfer_locked(&s_transfers[i]);
    clear_layer_locked(0);
    s_state.generation=0; s_state.elapsed_ms=s_state.duration_ms=0;
    s_state.connected=s_state.playing=s_state.lyrics_available=false;
    s_meta_us=0;
}
static void expire_locked(int64_t now) {
    for (unsigned i=0; i<2; ++i) {
        transfer_t *t=&s_transfers[i];
        if (t->bytes && (now-t->chunk_us>TRANSFER_IDLE_US || now-t->started_us>TRANSFER_TOTAL_US))
            cancel_transfer_locked(t);
    }
    if (s_state.connected && now-s_meta_us>BRIDGE_IDLE_US) reset_locked();
}
static bool allowed_locked(uint16_t connection) {
    return s_visible && s_encrypted && connection==s_connection && connection!=NO_CONNECTION;
}

void remote_media_set_visible(bool visible) {
    // 首次初始化来自 UI 线程、发生在 BLE 开启前。/ UI initializes before enabling BLE.
    if (!s_lock) { if (!visible) return; s_lock=xSemaphoreCreateMutex(); if (!s_lock) return; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_visible!=visible) { s_visible=visible; reset_locked(); }
    xSemaphoreGive(s_lock);
}
void remote_media_reset(void) {
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY); reset_locked(); xSemaphoreGive(s_lock);
}
void remote_media_set_link(uint16_t connection, bool encrypted) {
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (connection!=s_connection || encrypted!=s_encrypted) reset_locked();
    s_connection=connection; s_encrypted=encrypted;
    xSemaphoreGive(s_lock);
}
void remote_media_tick(void) {
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY); expire_locked(esp_timer_get_time()); xSemaphoreGive(s_lock);
}
void remote_media_get_snapshot(remote_media_snapshot_t *out) {
    if (!out) return;
    memset(out,0,sizeof *out); if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int64_t now=esp_timer_get_time(); expire_locked(now); *out=s_state;
    if (out->connected && out->playing) {
        uint64_t elapsed=(uint64_t)out->elapsed_ms+(uint64_t)(now-s_meta_us)/1000u;
        if (out->duration_ms && elapsed>out->duration_ms) elapsed=out->duration_ms;
        out->elapsed_ms=elapsed>UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
    }
    xSemaphoreGive(s_lock);
}
static bool copy_layer(void *dst, size_t capacity, uint32_t revision, bool artwork) {
    size_t need=artwork ? REMOTE_MEDIA_ARTWORK_BYTES : REMOTE_MEDIA_OVERLAY_BYTES;
    if (!dst || capacity<need || !s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY); expire_locked(esp_timer_get_time());
    uint8_t *src=artwork ? s_artwork : s_overlay;
    bool valid=s_state.connected && src && revision==(artwork ? s_state.artwork_revision : s_state.overlay_revision);
    if (valid) memcpy(dst,src,need);
    xSemaphoreGive(s_lock); return valid;
}
bool remote_media_copy_artwork(void *dst, size_t capacity, uint32_t revision) {
    return copy_layer(dst,capacity,revision,true);
}
bool remote_media_copy_overlay(void *dst, size_t capacity, uint32_t revision) {
    return copy_layer(dst,capacity,revision,false);
}
bool remote_media_status(uint16_t connection, uint8_t out[20]) {
    if (!s_lock || !out) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY); expire_locked(esp_timer_get_time());
    bool allowed=allowed_locked(connection);
    if (allowed) {
        memset(out,0,20); out[0]='S'; out[1]='R'; out[2]=1;
        out[3]=s_state.connected ? 1 : 0;
        write32(out+4,s_state.generation); write32(out+8,s_transfers[0].offset);
        write32(out+12,s_transfers[1].offset); write32(out+16,s_state.revision);
    }
    xSemaphoreGive(s_lock); return allowed;
}
bool remote_media_receive(uint16_t connection, const uint8_t *packet, size_t length) {
    if (!s_lock || !packet || length<16 || length>REMOTE_MEDIA_PACKET_MAX ||
        packet[0]!='S' || packet[1]!='R' || packet[2]!=1) return false;
    uint8_t op=packet[3]; uint32_t generation=read32(packet+4), offset=read32(packet+8), value=read32(packet+12);
    const uint8_t *data=packet+16; size_t n=length-16;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int64_t now=esp_timer_get_time(); expire_locked(now);
    bool ok=false;
    if (!allowed_locked(connection)) goto done;
    if (op==OP_RESET) {
        if (n==0 && generation==0 && offset==0 && value==0) { reset_locked(); ok=true; }
        goto done;
    }
    if (!generation) goto done;
    if (op==OP_META) {
        if (n!=12 || offset || value || (read32(data)&~3u)) goto done;
        if (s_state.generation && generation!=s_state.generation && (int32_t)(generation-s_state.generation)<=0) goto done;
        if (s_state.generation!=generation) { reset_locked(); s_state.generation=generation; }
        s_state.playing=(read32(data)&1u)!=0; s_state.lyrics_available=(read32(data)&2u)!=0;
        s_state.elapsed_ms=read32(data+4); s_state.duration_ms=read32(data+8);
        if (s_state.duration_ms && s_state.elapsed_ms>s_state.duration_ms) s_state.elapsed_ms=s_state.duration_ms;
        s_state.connected=true; s_meta_us=now; ++s_state.revision; ok=true; goto done;
    }
    if (!s_state.connected || generation!=s_state.generation) goto done;
    switch (op) {
    case OP_BEGIN: {
        size_t need=offset==LAYER_ARTWORK ? REMOTE_MEDIA_ARTWORK_BYTES :
                    offset==LAYER_OVERLAY ? REMOTE_MEDIA_OVERLAY_BYTES : 0;
        if (n!=4 || !need || value!=need) break;
        transfer_t *t=&s_transfers[offset-1]; cancel_transfer_locked(t);
        t->bytes=heap_caps_malloc(need,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if (!t->bytes) break;
        t->size=value; t->expected_crc=read32(data); t->crc=UINT32_MAX;
        t->started_us=t->chunk_us=now; ok=true; break;
    }
    case OP_DATA: {
        if (value<1 || value>2) break;
        transfer_t *t=&s_transfers[value-1];
        if (!t->bytes || !n || offset!=t->offset || n>t->size-t->offset) break;
        memcpy(t->bytes+t->offset,data,n); t->crc=crc_update(t->crc,data,n);
        t->offset+=(uint32_t)n; t->chunk_us=now; ok=true; break;
    }
    case OP_COMMIT: {
        if (n || value || offset<1 || offset>2) break;
        transfer_t *t=&s_transfers[offset-1];
        if (!t->bytes || t->offset!=t->size) break;
        if ((t->crc^UINT32_MAX)!=t->expected_crc) { cancel_transfer_locked(t); break; }
        // 完整校验后交换所有权，UI 从不看到半张图。/ Publish only a complete validated layer.
        if (offset==LAYER_ARTWORK) {
            heap_caps_free(s_artwork); s_artwork=t->bytes; s_state.has_artwork=true; ++s_state.artwork_revision;
        } else {
            heap_caps_free(s_overlay); s_overlay=t->bytes; s_state.has_overlay=true; ++s_state.overlay_revision;
        }
        t->bytes=NULL; cancel_transfer_locked(t); ++s_state.revision; ok=true; break;
    }
    case OP_CLEAR:
        if (n || value || offset>2) break;
        for (unsigned i=0; i<2; ++i) if (offset==0 || offset==i+1) cancel_transfer_locked(&s_transfers[i]);
        clear_layer_locked(offset); ok=true; break;
    default: break;
    }
done:
    xSemaphoreGive(s_lock); return ok;
}
