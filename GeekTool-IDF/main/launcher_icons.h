#pragma once
#include "lvgl_compat.h"

// Matches the launcher registry; navigation, names and app identity stay in launcher.c.
typedef enum {
    LAUNCHER_WIFI, LAUNCHER_SCAN, LAUNCHER_SYSTEM, LAUNCHER_WEATHER,
    LAUNCHER_CALENDAR, LAUNCHER_COUNTDOWN, LAUNCHER_STOPWATCH, LAUNCHER_SETTINGS,
    LAUNCHER_OTA, LAUNCHER_AUDIO, LAUNCHER_LEVEL, LAUNCHER_MAZE,
    LAUNCHER_FLUID, LAUNCHER_DICE, LAUNCHER_REMOTE, LAUNCHER_TWIN,
    LAUNCHER_ICON_COUNT,
    LAUNCHER_PREV = LAUNCHER_ICON_COUNT, LAUNCHER_NEXT
} launcher_icon_t;

#define LAUNCHER_ICON_SIZE 148
#define LAUNCHER_FRAME_COLOR 0xffffff

lv_obj_t *launcher_icon_create(lv_obj_t *parent, launcher_icon_t kind);
void launcher_icon_set(lv_obj_t *icon, launcher_icon_t kind);
