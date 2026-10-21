#pragma once
#include "lvgl.h"
#include "esp_pm.h"

typedef struct {esp_pm_lock_handle_t lock;bool held;} render_power_t;

static inline void render_power_event(lv_event_t *event) {
    render_power_t *power=lv_event_get_user_data(event);
    lv_event_code_t code=lv_event_get_code(event);
    if(code==LV_EVENT_RENDER_START && power->lock && !power->held) {
        power->held=esp_pm_lock_acquire(power->lock)==ESP_OK;
    } else if((code==LV_EVENT_RENDER_READY || code==LV_EVENT_DELETE) && power->held) {
        esp_pm_lock_release(power->lock);power->held=false;
    }
    if(code==LV_EVENT_DELETE && power->lock){esp_pm_lock_delete(power->lock);power->lock=NULL;}
}
static inline bool render_power_bind(lv_display_t *display,render_power_t *power) {
    // 仅真实脏区域绘制期间升频；不随20ms空闲轮询或AOD等待长期持锁。
    // Boost only while dirty areas render, never throughout idle polling or the AOD wait.
    if(esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX,0,"render",&power->lock)!=ESP_OK)return false;
    lv_display_add_event_cb(display,render_power_event,LV_EVENT_ALL,power);return true;
}
