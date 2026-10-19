#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define CAPSULE_RATE 16000u
#define CAPSULE_MAX_SECONDS 120u
#define CAPSULE_MAX_BYTES (CAPSULE_RATE * 2u * CAPSULE_MAX_SECONDS)
#define CAPSULE_HEADER_BYTES 44u
#define CAPSULE_PATH_BYTES 32u

static inline uint32_t capsule_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static inline void capsule_put32(uint8_t *p,uint32_t n) {
    for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(i*8));
}
static inline bool capsule_name_valid(const char *s) {
    if(!s || strlen(s)!=12 || strcmp(s+8,".WAV"))return false;
    for(unsigned i=0;i<8;++i)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='A'&&s[i]<='F')))return false;
    return true;
}
static inline bool capsule_path_valid(const char *s) {
    return s && !strncmp(s,"/caps/",6) && capsule_name_valid(s+6);
}
// 固定规范头避免解析任意格式与无界 chunk。 / Canonical headers avoid arbitrary formats and unbounded chunks.
static inline bool capsule_wav_header(uint8_t h[44],uint32_t bytes) {
    if(bytes>CAPSULE_MAX_BYTES || (bytes&1))return false;
    memset(h,0,44);memcpy(h,"RIFF",4);capsule_put32(h+4,bytes+36);
    memcpy(h+8,"WAVEfmt ",8);capsule_put32(h+16,16);h[20]=1;h[22]=1;
    capsule_put32(h+24,CAPSULE_RATE);capsule_put32(h+28,CAPSULE_RATE*2);
    h[32]=2;h[34]=16;memcpy(h+36,"data",4);capsule_put32(h+40,bytes);return true;
}
static inline bool capsule_wav_parse(const uint8_t h[44],size_t file_size,uint32_t *bytes) {
    uint32_t n=capsule_le32(h+40);uint8_t expected[44];
    if(!n || !capsule_wav_header(expected,n) || memcmp(h,expected,44) || file_size!=44u+n)return false;
    if(bytes)*bytes=n;
    return true;
}
static inline bool capsule_wav_open(FILE *f,uint32_t *bytes) {
    uint8_t h[44];if(!f || fseek(f,0,SEEK_END))return false;
    long n=ftell(f);if(n<44 || n>(long)(CAPSULE_MAX_BYTES+44) || fseek(f,0,SEEK_SET))return false;
    return fread(h,1,44,f)==44 && capsule_wav_parse(h,(size_t)n,bytes);
}
