#pragma once
#include "FreeRTOS.h"
#include <stddef.h>
typedef struct test_queue *QueueHandle_t;
QueueHandle_t xQueueCreate(UBaseType_t,size_t);
BaseType_t xQueueOverwrite(QueueHandle_t,const void *);
BaseType_t xQueueReceive(QueueHandle_t,void *,TickType_t);
void vQueueDelete(QueueHandle_t);
