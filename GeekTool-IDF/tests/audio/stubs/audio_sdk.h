#pragma once
#include "sdk.h"
typedef void *TaskHandle_t;
typedef unsigned TickType_t;
#define pdTRUE 1
#define portMAX_DELAY UINT32_MAX
unsigned ulTaskNotifyTake(int, unsigned);
void xTaskNotifyGive(TaskHandle_t);
