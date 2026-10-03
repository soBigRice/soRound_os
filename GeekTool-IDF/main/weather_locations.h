#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    uint32_t id;
    int32_t lat_e5, lon_e5;
    uint16_t parent, name, pinyin;
} wx_location_t;
extern const wx_location_t wx_locations[];
extern const uint16_t wx_location_count;
extern const char wx_location_names[];
uint16_t wx_location_find(uint32_t id); // 0 is root/invalid, never a selected city.
const char *wx_location_name(uint16_t index, bool chinese);
size_t wx_location_children(uint16_t parent, uint16_t *out, size_t capacity);
void wx_location_init(void); // Lazy load, NVS namespace independent of system settings.
uint16_t wx_location_selected(void);
bool wx_location_select(uint16_t index); // Persists atomically; false leaves RAM unchanged.
size_t wx_location_recent(uint16_t out[3]);
