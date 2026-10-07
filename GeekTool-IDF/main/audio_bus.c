#include "audio_bus.h"
#include "freertos/semphr.h"
static StaticSemaphore_t s_storage;
static SemaphoreHandle_t s_mutex;
void audio_bus_init(void) { if (!s_mutex) s_mutex = xSemaphoreCreateMutexStatic(&s_storage); }
bool audio_bus_ready(void) { return s_mutex != NULL; }
bool audio_bus_acquire(TickType_t wait) { return s_mutex && xSemaphoreTake(s_mutex, wait) == pdTRUE; }
void audio_bus_release(void) { xSemaphoreGive(s_mutex); }
