#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_ANY 255
#define ESP_PARTITION_SUBTYPE_DATA_FAT 129
typedef struct {uint32_t address,size;bool encrypted;} esp_partition_t;
const esp_partition_t *esp_partition_find_first(int type,int subtype,const char *name);
esp_err_t esp_partition_read(const esp_partition_t *,size_t,void *,size_t);
