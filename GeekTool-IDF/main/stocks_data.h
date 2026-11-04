#pragma once
#include <stdbool.h>
#include <stddef.h>

#define STOCK_SYMBOL_BYTES 16
#define STOCK_LIST_MAX 8
#define STOCK_HTTP_BYTES 32768

typedef struct { unsigned count; char symbols[STOCK_LIST_MAX][STOCK_SYMBOL_BYTES]; } stock_list_t;
typedef struct {
    char symbol[STOCK_SYMBOL_BYTES], name[96];
    char price[24], change[24], percent[24], timestamp[20], currency[4];
    double direction;
} stock_quote_t;

// 前缀明确区分同号指数/股票；不推断000001的市场。/ Explicit prefixes disambiguate equal numeric codes.
bool stock_symbol_normalize(const char *input, char out[STOCK_SYMBOL_BYTES]);
bool stock_quote_parse(const char *json, size_t bytes, const char *symbol, stock_quote_t *out);
bool stock_list_load(stock_list_t *out);
bool stock_list_save(const stock_list_t *list);
