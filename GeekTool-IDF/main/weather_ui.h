#pragma once
#include "lvgl.h"
#include <stdbool.h>

// 天气页独立的平滑字库;不改变启动器/其他 App 的既有点阵字体。
LV_FONT_DECLARE(font_weather_20);
LV_FONT_DECLARE(font_weather_16);

typedef struct {
    lv_obj_t *icon, *temperature, *condition, *range, *humidity, *status;
    int code, temp;
    bool is_day, has_data;
} weather_ui_t;

void weather_ui_create(weather_ui_t *ui, lv_obj_t *parent);
void weather_ui_show(weather_ui_t *ui, int temp, int low, int high,
                     int code, int humidity, bool is_day);
typedef enum {WEATHER_LOADING,WEATHER_OFFLINE,WEATHER_FETCH_FAILED} weather_status_t;
void weather_ui_status(weather_ui_t *ui, weather_status_t status);
const char *weather_condition_text(int code, bool is_day);
bool weather_code_supported(int code);
