#pragma once
#include "stocks_data.h"
#include <stdint.h>
typedef enum { STOCK_LOADING,STOCK_READY,STOCK_FAILED,STOCK_OFFLINE,STOCK_BUSY } stock_fetch_state_t;
typedef struct {
    stock_list_t list;
    stock_quote_t quotes[STOCK_LIST_MAX];
    stock_fetch_state_t states[STOCK_LIST_MAX];
    uint32_t revision;
    bool complete;
} stock_batch_t;
// 一轮覆盖整份自选，逐项发布结果。/ Refresh the entire watchlist and publish each result.
stock_fetch_state_t stocks_fetch_begin(const stock_list_t *list,uint32_t *token);
bool stocks_fetch_poll(uint32_t token,stock_batch_t *out);
void stocks_fetch_cancel(void);
