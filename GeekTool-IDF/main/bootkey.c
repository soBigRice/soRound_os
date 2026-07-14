// BOOT 实体键(GPIO0)轮询。GPIO0 是 strapping 脚,外部已有上拉,输入模式对启动无影响。
#include "bootkey.h"
#include "board_config.h"
#include "driver/gpio.h"
#include "esp_timer.h"

static int     s_prev = 1;     // 上次电平(1=松开)
static int64_t s_last_us;      // 上次触发时刻

void bootkey_init(void) {
    gpio_config_t io = { .pin_bit_mask = 1ULL << BOOT_BUTTON, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&io);
    s_prev = 1;                // 视为松开:防止按着键进 app 误触发
    s_last_us = 0;
}

bool bootkey_pressed(void) {
    int lv = gpio_get_level(BOOT_BUTTON);
    int64_t now = esp_timer_get_time();
    bool hit = (s_prev == 1 && lv == 0 && now - s_last_us > 200000);
    if (hit) s_last_us = now;
    s_prev = lv;
    return hit;
}
