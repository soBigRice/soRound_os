#pragma once
#include <stdlib.h>
#define MALLOC_CAP_8BIT 1
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_SPIRAM 4
static inline void *heap_caps_malloc(size_t size,unsigned caps){(void)caps;return malloc(size);}
static inline void heap_caps_free(void *p){free(p);}
