#pragma once
#include "FreeRTOS.h"
#include <stdlib.h>
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    SemaphoreHandle_t mutex=malloc(sizeof *mutex);
    if (mutex) pthread_mutex_init(mutex,NULL);
    return mutex;
}
static inline void xSemaphoreTake(SemaphoreHandle_t mutex, int timeout) { (void)timeout; pthread_mutex_lock(mutex); }
static inline void xSemaphoreGive(SemaphoreHandle_t mutex) { pthread_mutex_unlock(mutex); }
