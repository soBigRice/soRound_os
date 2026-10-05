#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define ZODIAC_COUNT 12
#define ZODIAC_HTTP_BYTES 8192
#define ZODIAC_BODY_BYTES 1024
typedef struct {
    uint8_t sign,score[5];
    char date[32],comment[192],body[5][ZODIAC_BODY_BYTES];
    char color[64],number[24],partner[64],good[192],avoid[192];
} zodiac_data_t;
typedef enum {ZODIAC_IDLE,ZODIAC_LOADING,ZODIAC_READY,ZODIAC_OFFLINE,ZODIAC_FAILED,ZODIAC_BUSY} zodiac_state_t;
extern const char *const ZODIAC_KEYS[ZODIAC_COUNT];
extern const char *const ZODIAC_EN[ZODIAC_COUNT];
extern const char *const ZODIAC_ZH[ZODIAC_COUNT];
bool zodiac_data_parse(const char *json,size_t length,unsigned sign,zodiac_data_t *out);
zodiac_state_t zodiac_fetch_begin(unsigned sign,uint32_t *token);
zodiac_state_t zodiac_fetch_poll(uint32_t token,zodiac_data_t *out);
void zodiac_fetch_cancel(void);
