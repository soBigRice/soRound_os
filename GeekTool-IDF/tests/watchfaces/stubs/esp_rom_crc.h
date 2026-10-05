#pragma once
#include <stdint.h>
// Host implementation of the IDF ROM CRC-32/ISO-HDLC contract.
static inline uint32_t esp_rom_crc32_le(uint32_t crc,const uint8_t *buf,uint32_t len) {
    crc=~crc;
    for(uint32_t i=0;i<len;++i) {
        crc^=buf[i];
        for(int bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1)?0xedb88320u:0);
    }
    return ~crc;
}
