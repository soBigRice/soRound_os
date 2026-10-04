#pragma once
#include <stdbool.h>
#include <stddef.h>

#define WX_HOURS 12
#define WX_DAYS 5
typedef struct {
    char time[6]; // Location-local HH:MM, not the device's time zone.
    float temperature, probability, precipitation;
    int code;
    bool is_day;
} weather_hour_t;
typedef struct {
    char date[11], sunrise[6], sunset[6];
    float low, high, probability, precipitation, uv, daylight;
    int code;
} weather_day_t;
typedef struct {
    int temp, low, high, humidity, code;
    bool is_day, valid;
    char updated[6];
    float apparent, wind, gust, direction, precipitation, cloud, pressure, visibility;
    weather_hour_t hours[WX_HOURS];
    weather_day_t days[WX_DAYS];
    unsigned hour_count, day_count;
} weather_data_t;

// NUL-terminated JSON, length excludes NUL. Optional numbers remain NAN; missing core fields reject the response.
bool weather_data_parse(const char *json, size_t length, weather_data_t *data);
int weather_data_url(char *out, size_t size, double latitude, double longitude);
