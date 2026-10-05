#pragma once
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1
void *heap_caps_malloc(size_t size,unsigned flags);
