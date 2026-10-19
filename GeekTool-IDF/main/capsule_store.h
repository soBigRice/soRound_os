#pragma once
#include "capsule_wav.h"
#include <time.h>

#define CAPSULE_MAX_FILES 32
#define CAPSULE_MIN_FREE (64u*1024u)
typedef enum { CAPSULE_OK, CAPSULE_UNCONFIGURED, CAPSULE_CORRUPT_STORAGE,
               CAPSULE_IO_ERROR, CAPSULE_FULL, CAPSULE_EMPTY, CAPSULE_INVALID } capsule_result_t;
typedef struct { char name[13];uint32_t bytes;time_t created;bool valid; } capsule_entry_t;
typedef struct {
    capsule_entry_t entries[CAPSULE_MAX_FILES];unsigned count,damaged;
    uint32_t free_bytes;bool recovered;
} capsule_catalog_t;
// 单一录音任务调用；UI 只读快照。 / Called by one recorder task; UI reads snapshots only.
capsule_result_t capsule_store_mount(void);
void capsule_store_unmount(void);
capsule_result_t capsule_store_list(capsule_catalog_t *catalog);
capsule_result_t capsule_store_begin(void);
capsule_result_t capsule_store_write(const int16_t *pcm,size_t samples);
capsule_result_t capsule_store_finish(bool save);
capsule_result_t capsule_store_delete(const char *name);
bool capsule_store_path(const char *name,char path[CAPSULE_PATH_BYTES]);

// 对真实分区仅做只读验证；禁止把 wl_mount 当无损探测。
// Read-only guard validation; never use wl_mount as a non-destructive probe.
bool capsule_store_meta_valid(const uint8_t meta[32]);
bool capsule_store_wl_state_valid(const uint8_t state[64]);
