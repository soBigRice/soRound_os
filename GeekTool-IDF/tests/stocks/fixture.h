#pragma once
#include <assert.h>
#include <stdio.h>
#include "stocks_data.h"
static size_t stock_fixture(const char *symbol,char out[STOCK_HTTP_BYTES]) {
    char path[512];snprintf(path,sizeof path,"%s/%s.json",STOCK_FIXTURES,symbol);
    FILE *f=fopen(path,"rb");assert(f);size_t n=fread(out,1,STOCK_HTTP_BYTES-1,f);assert(!ferror(f));assert(!fclose(f));out[n]=0;return n;
}
