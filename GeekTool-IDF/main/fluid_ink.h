#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { FLUID_INK_WIDTH=466, FLUID_INK_GRID=64, FLUID_INK_PALETTES=3, FLUID_INK_BANDS=8 };
typedef struct fluid_ink fluid_ink_t;
typedef struct {int16_t x1,y1,x2,y2;} fluid_ink_dirty_t;
size_t fluid_ink_bytes(void);
void fluid_ink_reset(fluid_ink_t *ink,unsigned palette);
// Normalized screen coordinates, y downward; velocity is grid cells/second.
void fluid_ink_inject(fluid_ink_t *ink,float x,float y,float dx,float dy,unsigned color,float dose);
float fluid_ink_step(fluid_ink_t *ink,float seconds);
bool fluid_ink_render(fluid_ink_t *ink,uint16_t *rgb565,fluid_ink_dirty_t dirty[FLUID_INK_BANDS]);
