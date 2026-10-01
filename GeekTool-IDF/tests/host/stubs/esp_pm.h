#pragma once
#include <stdlib.h>
#include <assert.h>
#define ESP_OK 0
#define ESP_PM_CPU_FREQ_MAX 0
typedef struct { int held; } *esp_pm_lock_handle_t;
static inline int esp_pm_lock_create(int type, int arg, const char *name, esp_pm_lock_handle_t *h) {
    (void)type; (void)arg; (void)name; *h = calloc(1, sizeof **h); return *h ? 0 : -1;
}
static inline int esp_pm_lock_acquire(esp_pm_lock_handle_t h) { assert(!h->held); h->held++; return 0; }
static inline int esp_pm_lock_release(esp_pm_lock_handle_t h) { assert(h->held == 1); h->held--; return 0; }
static inline int esp_pm_lock_delete(esp_pm_lock_handle_t h) { assert(!h->held); free(h); return 0; }
