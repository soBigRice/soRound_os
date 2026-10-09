#pragma once
#include <stdbool.h>
#include <stdint.h>

#define PIXEL_FIELD_MAX_TILES (25 * 15)
#define PIXEL_FIELD_PATTERNS 3
#define PIXEL_FIELD_PALETTES 3
#define PIXEL_FIELD_TILE_SIZE 12

// Screen coordinates in native 466x466 pixels, not sensor axes or grid indices.
typedef struct {
    float x, y, vx, vy;
    int16_t tx, ty;
    int8_t color;                 // -1 is an unlit cell, otherwise palette index 0..5.
} pixel_field_tile_t;
typedef struct {
    pixel_field_tile_t tiles[PIXEL_FIELD_MAX_TILES];
    uint16_t count;
    uint8_t pattern;
    bool active, touching;
    float touch_x, touch_y;
} pixel_field_t;
typedef struct {
    uint32_t peak_at, fired_at, quiet_at;
    bool peak, fell, blocked, quiet;
} pixel_shake_t;

void pixel_field_init(pixel_field_t *field, unsigned pattern);
void pixel_field_repattern(pixel_field_t *field, unsigned pattern, bool scatter);
void pixel_field_touch(pixel_field_t *field, float x, float y, bool touching);
void pixel_field_gather(pixel_field_t *field);
// Bounded spring motion; false means no tile's raster position changed.
bool pixel_field_step(pixel_field_t *field, float seconds);
void pixel_shake_reset(pixel_shake_t *shake);
// Acceleration is in g. Two separated peaks trigger once; quiet + cooldown rearm.
bool pixel_shake_sample(pixel_shake_t *shake, float x, float y, float z, uint32_t now_ms);
