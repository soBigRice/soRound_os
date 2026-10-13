#pragma once
#include "lvgl.h"
#include "weather_frame.h"
typedef struct {
    weather_frame_t frame;
    // Borrow the two fixed LVGL partial buffers only after the last tile has
    // rendered. Keep the final flush outstanding until DMA releases both.
    struct {uint8_t *pixels;size_t capacity;} staging[2];
    void (*wait_te)(void *arg);
    void (*send)(void *arg,weather_frame_area_t area,uint8_t *pixels);
    // If supplied, send retains pixels until wait_send returns. Without it,
    // send is synchronous, preserving the original single-buffer interface.
    void (*wait_send)(void *arg);
    void *arg;
} weather_refresh_t;
// false leaves the original asynchronous partial path responsible for this flush.
bool weather_refresh_flush(weather_refresh_t *refresh,lv_display_t *disp,const lv_area_t *area,uint8_t *pixels);
