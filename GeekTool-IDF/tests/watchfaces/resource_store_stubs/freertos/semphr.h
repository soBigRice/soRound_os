#pragma once
#include "FreeRTOS.h"
#include <stdbool.h>
typedef struct {pthread_mutex_t native;bool initialized;} StaticSemaphore_t;
typedef StaticSemaphore_t *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *storage);
int xSemaphoreTake(SemaphoreHandle_t mutex,TickType_t ticks);
void xSemaphoreGive(SemaphoreHandle_t mutex);
