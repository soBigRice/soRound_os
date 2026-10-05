#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ANSWER_TEXT_BYTES 192
#define ANSWER_HTTP_BYTES 4096
#define ANSWER_API_URL "https://v2.xxapi.cn/api/answers?question=Next%20page"
typedef struct { char en[ANSWER_TEXT_BYTES], zh[ANSWER_TEXT_BYTES]; } answer_response_t;
typedef enum { ANSWER_FETCH_IDLE, ANSWER_FETCH_LOADING, ANSWER_FETCH_READY,
               ANSWER_FETCH_OFFLINE, ANSWER_FETCH_FAILED, ANSWER_FETCH_BUSY,
               ANSWER_FETCH_UNSUPPORTED, ANSWER_FETCH_REPEAT } answer_fetch_state_t;

bool answers_data_parse(const char *json, size_t length, answer_response_t *out);
answer_fetch_state_t answers_fetch_begin(uint32_t *token);
answer_fetch_state_t answers_fetch_poll(uint32_t token, answer_response_t *out);
void answers_fetch_cancel(void);
