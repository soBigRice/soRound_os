#pragma once
#include <stdint.h>
extern int64_t test_time_us;
static inline int64_t esp_timer_get_time(void) { return test_time_us; }
