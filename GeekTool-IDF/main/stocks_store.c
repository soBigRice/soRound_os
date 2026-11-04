#include "stocks_data.h"
#include "nvs.h"
#include <stdint.h>
#include <string.h>

// 独立、定长、带版本的记录；不迁移或覆盖现有设置。/ Separate versioned bytes leave existing settings untouched.
#define RECORD_BYTES (2+STOCK_LIST_MAX*STOCK_SYMBOL_BYTES)
static bool valid(const stock_list_t *list) {
    if(!list || list->count>STOCK_LIST_MAX)return false;
    for(unsigned i=0;i<list->count;++i) {
        char symbol[STOCK_SYMBOL_BYTES];
        if(!memchr(list->symbols[i],0,STOCK_SYMBOL_BYTES)||!stock_symbol_normalize(list->symbols[i],symbol)||strcmp(symbol,list->symbols[i]))return false;
        for(unsigned j=0;j<i;++j)if(!strcmp(symbol,list->symbols[j]))return false;
    }return true;
}
bool stock_list_load(stock_list_t *out) {
    if(!out)return false;
    nvs_handle_t handle;esp_err_t err=nvs_open("stocks",NVS_READONLY,&handle);
    uint8_t record[RECORD_BYTES]={0};size_t size=sizeof record;
    if(err==ESP_OK){err=nvs_get_blob(handle,"watchlist",record,&size);nvs_close(handle);}
    if(err==ESP_ERR_NVS_NOT_FOUND){*out=(stock_list_t){0};return true;}
    if(err!=ESP_OK || size!=sizeof record || record[0]!=1 || record[1]>STOCK_LIST_MAX)return false;
    stock_list_t list={.count=record[1]};memcpy(list.symbols,record+2,sizeof list.symbols);
    if(!valid(&list))return false;
    *out=list;return true;
}
bool stock_list_save(const stock_list_t *list) {
    if(!valid(list))return false;
    uint8_t record[RECORD_BYTES]={1,(uint8_t)list->count};
    for(unsigned i=0;i<list->count;++i)memcpy(record+2+i*STOCK_SYMBOL_BYTES,list->symbols[i],strlen(list->symbols[i])+1);
    nvs_handle_t handle;esp_err_t err=nvs_open("stocks",NVS_READWRITE,&handle);
    if(err==ESP_OK){err=nvs_set_blob(handle,"watchlist",record,sizeof record);if(err==ESP_OK)err=nvs_commit(handle);nvs_close(handle);}
    return err==ESP_OK;
}
