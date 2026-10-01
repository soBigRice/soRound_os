#pragma once
#include <stdbool.h>
#include "freertos/FreeRTOS.h"

// I2S0 的独占权跟随采集/播放任务的硬件生命周期,UI 不等待硬件退出。
void audio_bus_init(void);
bool audio_bus_acquire(TickType_t wait);
void audio_bus_release(void);
