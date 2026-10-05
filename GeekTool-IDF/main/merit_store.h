#pragma once
#include <stdbool.h>
#include <stdint.h>
#define MERIT_MAX UINT32_C(999999999)
bool merit_load(uint32_t *count);
bool merit_save(uint32_t count);
