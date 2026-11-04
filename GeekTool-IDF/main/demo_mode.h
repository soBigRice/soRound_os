#pragma once
#include "app.h"

// 演示适配器只构建真实视图，不启动服务、不保存样例。/ Adapters reuse real views without services or sample writes.
typedef struct {
    unsigned count;
    void (*enter)(lv_obj_t *parent, unsigned variant);
    void (*frame)(uint32_t elapsed_ms);
    void (*exit)(void);
    void (*visibility)(bool visible);
    bool (*fullscreen)(unsigned variant);
} demo_page_t;

extern const app_t app_demo;
extern const demo_page_t demo_wifi, demo_i2c, demo_sys, demo_weather, demo_calendar;
extern const demo_page_t demo_countdown, demo_stopwatch, demo_settings, demo_ota;
extern const demo_page_t demo_audio, demo_level, demo_maze, demo_fluid, demo_dice;
extern const demo_page_t demo_mouse, demo_twin, demo_answers, demo_zodiac, demo_merit;
extern const demo_page_t demo_pixels, demo_garden, demo_focus, demo_capsules, demo_watchfaces;

// 由设置事件异步进入；所有接口仅在LVGL任务/锁内调用。/ Enter after event dispatch, under the LVGL lock.
void launcher_start_demo(void);
void launcher_demo_header(const app_t *source);
void launcher_demo_chrome(bool visible);
unsigned demo_mode_count(void);
unsigned demo_mode_index(void);
bool demo_mode_paused(void);
void demo_mode_step(int direction);
void demo_mode_pause(bool paused);
