#pragma once
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
extern bool test_alloc_fail;
extern unsigned test_allocations;
static inline void *heap_caps_malloc(size_t size, int caps) {
    (void)caps; if (test_alloc_fail) return NULL;
    void *p=malloc(size); if (p) ++test_allocations; return p;
}
static inline void heap_caps_free(void *p) { if (p) { --test_allocations; free(p); } }
