#pragma once
#include "freertos/FreeRTOS.h"
TickType_t xTaskGetTickCount(void);
void vTaskDelayUntil(TickType_t *wake, TickType_t period);
