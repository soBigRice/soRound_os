#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 固定图层契约；文字由 Mac CoreText 排版，固件不猜测动态中文字形。
// Fixed layers: Mac CoreText renders text; firmware needs no dynamic CJK font.
#define REMOTE_MEDIA_ARTWORK_WIDTH 466
#define REMOTE_MEDIA_ARTWORK_HEIGHT 466
#define REMOTE_MEDIA_ARTWORK_BYTES (466u * 466u * 2u)
#define REMOTE_MEDIA_OVERLAY_WIDTH 350
#define REMOTE_MEDIA_OVERLAY_HEIGHT 180
#define REMOTE_MEDIA_OVERLAY_BYTES (350u * 180u)
#define REMOTE_MEDIA_PACKET_MAX 512u
#define REMOTE_MEDIA_HEADER_BYTES 16u

typedef struct {
    uint32_t revision, artwork_revision, overlay_revision, generation;
    uint32_t elapsed_ms, duration_ms;
    bool connected, playing, has_artwork, has_overlay, lyrics_available;
} remote_media_snapshot_t;

void remote_media_set_visible(bool visible);
void remote_media_reset(void);
void remote_media_get_snapshot(remote_media_snapshot_t *out);
bool remote_media_copy_artwork(void *dst, size_t capacity, uint32_t expected_revision);
bool remote_media_copy_overlay(void *dst, size_t capacity, uint32_t expected_revision);

// BLE 桥接内部入口；UI 仅调用上方接口。/ Internal bridge entry points; UI uses the API above.
void remote_media_set_link(uint16_t connection, bool encrypted);
bool remote_media_receive(uint16_t connection, const uint8_t *packet, size_t length);
bool remote_media_status(uint16_t connection, uint8_t status[20]);
void remote_media_tick(void);
