#include "buttons.h"
#include "board_config.h"
#include "power.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_err.h"

#define DEBOUNCE_US 30000
#define LONG_PRESS_US 2000000
#define POWER_POLL_US 100000
#define CONTROL_EXPIRY_US 200000

static int s_raw, s_stable;
static int64_t s_raw_at, s_press_at, s_power_at, s_control_at;
static bool s_armed, s_long_sent, s_control;

void buttons_init(void) {
    // GPIO0 仍是输入,不改 BOOT strapping/烧录入口。上电时按住不触发软件关机。
    gpio_config_t io = { .pin_bit_mask = 1ULL << BOOT_BUTTON, .mode = GPIO_MODE_INPUT,
                         .pull_up_en = GPIO_PULLUP_ENABLE };
    ESP_ERROR_CHECK(gpio_config(&io));
    s_raw = s_stable = gpio_get_level(BOOT_BUTTON);
    s_armed = s_stable != 0;
    s_long_sent = s_control = false;
    s_raw_at = s_power_at = esp_timer_get_time();
    power_key_init();
}

void buttons_reset_control(void) { s_control = false; }

button_event_t buttons_poll(bool control_active) {
    int64_t now = esp_timer_get_time();
    int raw = gpio_get_level(BOOT_BUTTON);
    button_event_t event = BUTTON_NONE;
    if (raw != s_raw) { s_raw = raw; s_raw_at = now; }
    if (s_stable != raw && now - s_raw_at >= DEBOUNCE_US) {
        s_stable = raw;
        if (raw == 0) {
            s_press_at = s_raw_at; s_long_sent = false;
        } else {
            // 短按在松手后确认,长按不能同时触发锁屏。忙帧跨过阈值也仍判长按。
            if (s_armed && !s_long_sent)
                event = s_raw_at - s_press_at >= LONG_PRESS_US ? BUTTON_LONG : BUTTON_SHORT;
            s_armed = true;
        }
    }
    if (s_armed && s_stable == 0 && raw == 0 && !s_long_sent && now - s_press_at >= LONG_PRESS_US) {
        s_long_sent = true; event = BUTTON_LONG;
    }
    // PMU 硬件已去抖;保持原 100ms I2C 读取节奏,不因 GPIO 的 20ms 轮询增加总线压力。
    if (now - s_power_at >= POWER_POLL_US) {
        s_power_at = now;
        int power_event = power_key_event();
        if (power_event == 1 && control_active) { s_control = true; s_control_at = now; }
    }
    if (!control_active || event != BUTTON_NONE) s_control = false;
    return event;
}

bool buttons_control_pressed(void) {
    bool hit = s_control && esp_timer_get_time() - s_control_at <= CONTROL_EXPIRY_US;
    s_control = false;
    return hit;
}
