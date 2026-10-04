#pragma once
#include "sdk.h"
typedef struct {
    size_t total_free_bytes,total_allocated_bytes,largest_free_block,minimum_free_bytes;
} multi_heap_info_t;
void heap_caps_get_info(multi_heap_info_t *,uint32_t);
size_t heap_caps_get_total_size(uint32_t);
