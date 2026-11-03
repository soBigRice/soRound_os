#pragma once
#include <stddef.h>
#define MALLOC_CAP_INTERNAL 1u
#define MALLOC_CAP_SPIRAM 2u
#define MALLOC_CAP_8BIT 4u
void *heap_caps_malloc(size_t,unsigned);
void *heap_caps_calloc(size_t,size_t,unsigned);
void heap_caps_free(void *);
