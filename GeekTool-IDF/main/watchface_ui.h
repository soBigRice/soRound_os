#pragma once
#include "lvgl.h"
#include <stdbool.h>
#include <time.h>

// One data snapshot serves the active lock screen and its scaled settings preview.
typedef struct {
    struct tm time;
    bool aod, wifi, battery_valid, charging, weather_valid, image_loading;
    char ssid[33], ip[16];
    int battery, temperature, low, high, code, humidity;
    const lv_image_dsc_t *image;
} watchface_data_t;

enum { WATCHFACE_THEME_COUNT = 3, WATCHFACE_KIND_COUNT = 5, WATCHFACE_COUNT = 15 };
void watchface_render(lv_layer_t *layer, const lv_area_t *area,
                      const watchface_data_t *data, int index, bool preview);
LV_FONT_DECLARE(font_wf_7);
LV_FONT_DECLARE(font_wf_9);
LV_FONT_DECLARE(font_wf_14);
LV_FONT_DECLARE(font_wf_18);
LV_FONT_DECLARE(font_wf_26);
LV_FONT_DECLARE(font_wf_36);
LV_FONT_DECLARE(font_wf_52);
LV_FONT_DECLARE(font_wf_56);
LV_FONT_DECLARE(font_wf_72);
LV_FONT_DECLARE(font_wf_82);
LV_FONT_DECLARE(font_wf_104);
LV_FONT_DECLARE(font_wf_112);
LV_FONT_DECLARE(font_wf_164);
LV_FONT_DECLARE(font_wf_regular_48);
LV_FONT_DECLARE(font_wf_regular_96);
LV_FONT_DECLARE(font_wf_black_94);
LV_FONT_DECLARE(font_wf_black_188);
LV_FONT_DECLARE(font_wf_semibold_14);
LV_FONT_DECLARE(font_wf_semibold_26);
LV_FONT_DECLARE(font_wf_semibold_28);
LV_FONT_DECLARE(font_wf_semibold_52);
LV_FONT_DECLARE(font_wf_semibold_56);
LV_FONT_DECLARE(font_wf_semibold_104);
LV_FONT_DECLARE(font_wf_semibold_112);
