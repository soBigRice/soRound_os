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
typedef enum {
    ZODIAC_ERROR_NONE, ZODIAC_ERROR_MEMORY, ZODIAC_ERROR_CONNECTION,
    ZODIAC_ERROR_TLS, ZODIAC_ERROR_HTTP, ZODIAC_ERROR_TIMEOUT,
    ZODIAC_ERROR_RESPONSE, ZODIAC_ERROR_DATE
} zodiac_error_t;
typedef struct {
    zodiac_error_t reason;
    int http_status, requested_day;
    char response_date[32];
} zodiac_failure_t;
extern const char *const ZODIAC_KEYS[ZODIAC_COUNT];
extern const char *const ZODIAC_EN[ZODIAC_COUNT];
extern const char *const ZODIAC_ZH[ZODIAC_COUNT];
bool zodiac_data_parse(const char *json,size_t length,unsigned sign,zodiac_data_t *out);
zodiac_state_t zodiac_fetch_begin(unsigned sign,uint32_t *token);
zodiac_state_t zodiac_fetch_poll(uint32_t token,zodiac_data_t *out);
// 失败详情仅属于当前请求；过期token不返回旧诊断。 / Failure details belong only to the current request.
bool zodiac_fetch_failure(uint32_t token,zodiac_failure_t *out);
void zodiac_fetch_cancel(void);
