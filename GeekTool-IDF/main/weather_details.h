#pragma once
#include "weather_ui.h"
#include "weather_data.h"

typedef struct {
    lv_obj_t *scroll, *hero, *indicator;
    lv_obj_t *sections[4];
    weather_data_t data;
    float reveal[4];
    uint8_t indicator_opa;
    bool available;
} weather_details_t;

void weather_details_create(weather_details_t *view,lv_obj_t *parent);
void weather_details_show(weather_details_t *view,const weather_data_t *data,bool available);
void weather_details_reset(weather_details_t *view);
void weather_details_close(weather_details_t *view);
void weather_details_visibility(weather_details_t *view,bool visible);
