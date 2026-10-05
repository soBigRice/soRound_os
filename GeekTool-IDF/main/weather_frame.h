#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define WEATHER_FRAME_WIDTH 466
#define WEATHER_FRAME_HEIGHT 466
#define WEATHER_FRAME_BYTES (WEATHER_FRAME_WIDTH * WEATHER_FRAME_HEIGHT * 2u)
typedef struct {int x1,y1,x2,y2;} weather_frame_area_t;
typedef struct {uint8_t *pixels;bool dirty;weather_frame_area_t area;} weather_frame_t;
// Stored pixels already use the panel's RGB565_SWAPPED byte order. No colour conversion.
bool weather_frame_patch(weather_frame_t *frame,weather_frame_area_t area,const uint8_t *tile,size_t stride);
bool weather_frame_read(const weather_frame_t *frame,weather_frame_area_t area,uint8_t *tile,size_t capacity);
