#pragma once
#include <pthread.h>
#include <stdint.h>
typedef pthread_mutex_t portMUX_TYPE;
typedef uint32_t TickType_t;
extern _Thread_local unsigned fixture_critical_depth;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(m) do { pthread_mutex_lock(m); ++fixture_critical_depth; } while(0)
#define portEXIT_CRITICAL(m) do { --fixture_critical_depth; pthread_mutex_unlock(m); } while(0)
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
