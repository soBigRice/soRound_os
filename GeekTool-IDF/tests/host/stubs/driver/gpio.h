#pragma once
#include <stdint.h>
typedef struct { uint64_t pin_bit_mask; int mode, pull_up_en; } gpio_config_t;
#define GPIO_MODE_INPUT 1
#define GPIO_PULLUP_ENABLE 1
int gpio_config(const gpio_config_t *config);
int gpio_get_level(int pin);
