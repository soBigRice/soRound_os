#pragma once
#include <stdbool.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL (-1)
typedef struct {int max_files;bool format_if_mount_failed;} esp_vfs_fat_mount_config_t;
int esp_vfs_fat_spiflash_mount_ro(const char *,const char *,const esp_vfs_fat_mount_config_t *);
static inline const char *esp_err_to_name(esp_err_t value){(void)value;return "fixture";}
