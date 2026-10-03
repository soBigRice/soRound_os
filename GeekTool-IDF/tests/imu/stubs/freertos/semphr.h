#pragma once
#include "sdk.h"
typedef struct {bool held;} StaticSemaphore_t;
typedef StaticSemaphore_t *SemaphoreHandle_t;
#define pdTRUE 1
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *);
int xSemaphoreTake(SemaphoreHandle_t,int);
void xSemaphoreGive(SemaphoreHandle_t);
