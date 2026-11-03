#pragma once
#include <stdint.h>
#include <zlib.h>
static inline uint32_t esp_rom_crc32_le(uint32_t seed,const uint8_t *data,uint32_t length){return (uint32_t)crc32(seed,data,length);}
