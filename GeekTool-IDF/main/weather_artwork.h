#pragma once
#include "lvgl.h"
#include <stdbool.h>

typedef struct {
    int code;
    bool night;
    int x, y;                     // Within the existing 280x160 weather icon area.
    const lv_image_dsc_t *image;
} weather_artwork_t;

const weather_artwork_t *weather_artwork_for(int code, bool is_day);
