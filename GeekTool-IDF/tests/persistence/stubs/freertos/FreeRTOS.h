#pragma once
#include <stdint.h>
#include <assert.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) do{assert(*(m)==0);++*(m);}while(0)
#define portEXIT_CRITICAL(m) do{assert(*(m)==1);--*(m);}while(0)
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define portMAX_DELAY UINT32_MAX
#define pdTRUE 1
#define pdPASS 1
