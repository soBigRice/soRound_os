#pragma once
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>
extern const lv_font_t font_location_24;
// Callbacks run on the LVGL task. Successful selection closes the overlay.
void weather_location_ui_open(lv_obj_t *parent, bool (*select)(uint16_t));
bool weather_location_ui_back(void);
bool weather_location_ui_visible(void);
void weather_location_ui_close(void);
