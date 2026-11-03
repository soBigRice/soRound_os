#pragma once
#include "FreeRTOS.h"
typedef struct test_task *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
typedef enum {eRunning,eReady,eBlocked,eSuspended,eDeleted,eInvalid} eTaskState;
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t,const char *,uint32_t,void *,UBaseType_t,TaskHandle_t *,BaseType_t);
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t);
void vTaskSuspend(TaskHandle_t);
void vTaskDelete(TaskHandle_t);
eTaskState eTaskGetState(TaskHandle_t);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t);
