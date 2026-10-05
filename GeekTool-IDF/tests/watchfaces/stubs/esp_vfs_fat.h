#pragma once
#include <stdbool.h>
typedef int esp_err_t;
#define ESP_OK 0
typedef struct {int max_files;bool format_if_mount_failed;} esp_vfs_fat_mount_config_t;
int esp_vfs_fat_spiflash_mount_ro(const char *base,const char *partition,const esp_vfs_fat_mount_config_t *cfg);
static inline const char *esp_err_to_name(int error){(void)error;return "fixture";}
