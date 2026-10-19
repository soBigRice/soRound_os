#pragma once
#include "wear_levelling.h"
typedef struct {int max_files;bool format_if_mount_failed;size_t allocation_unit_size;} esp_vfs_fat_mount_config_t;
int esp_vfs_fat_spiflash_mount_rw_wl(const char *,const char *,const esp_vfs_fat_mount_config_t *,wl_handle_t *);
int esp_vfs_fat_spiflash_unmount_rw_wl(const char *,wl_handle_t);
int esp_vfs_fat_info(const char *,uint64_t *,uint64_t *);
