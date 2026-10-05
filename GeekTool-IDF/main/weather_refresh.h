#pragma once
#include "lvgl.h"
#include "weather_frame.h"
typedef struct {
    weather_frame_t frame;
    void (*wait_te)(void *arg);
    void (*send)(void *arg,weather_frame_area_t area,uint8_t *pixels);
    void *arg;
} weather_refresh_t;
// false leaves the original asynchronous partial path responsible for this flush.
bool weather_refresh_flush(weather_refresh_t *refresh,lv_display_t *disp,const lv_area_t *area,uint8_t *pixels);
