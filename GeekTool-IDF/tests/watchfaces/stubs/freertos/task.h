#pragma once
#include "FreeRTOS.h"
int xTaskCreate(void (*callback)(void *),const char *name,uint32_t stack,void *arg,unsigned priority,void *handle);
void vTaskDelete(void *handle);
