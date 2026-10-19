#pragma once
#include "esp_partition.h"
typedef int wl_handle_t;
#define WL_INVALID_HANDLE -1
int wl_unmount(wl_handle_t);
