#pragma once
#include <assert.h>
#define ESP_OK 0
#define ESP_ERROR_CHECK(expression) assert((expression) == ESP_OK)
