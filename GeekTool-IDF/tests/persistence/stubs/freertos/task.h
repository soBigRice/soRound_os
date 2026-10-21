#pragma once
#include "FreeRTOS.h"
#include <stddef.h>
typedef void *TaskHandle_t;
TickType_t xTaskGetTickCount(void);
BaseType_t xTaskCreate(void (*worker)(void *),const char *,unsigned,void *,unsigned,TaskHandle_t *);
void xTaskNotifyGive(TaskHandle_t);
unsigned ulTaskNotifyTake(BaseType_t,TickType_t);
void vTaskDelay(TickType_t);
