#include "fixture.h"
#include "cJSON.h"
#include "nvs.h"
#include <string.h>
#include <stdlib.h>
static uint8_t stored[130],pending[130];
static bool exists,fail_read,fail_write,fail_commit;
static unsigned opens,closes,writes;
esp_err_t nvs_open(const char *ns,int mode,nvs_handle_t *out){assert(!strcmp(ns,"stocks"));assert(mode==NVS_READONLY||mode==NVS_READWRITE);*out=1;++opens;return ESP_OK;}
esp_err_t nvs_get_blob(nvs_handle_t n,const char *key,void *out,size_t *bytes){assert(n==1&&!strcmp(key,"watchlist"));if(fail_read)return ESP_FAIL;if(!exists)return ESP_ERR_NVS_NOT_FOUND;assert(*bytes>=sizeof stored);memcpy(out,stored,sizeof stored);*bytes=sizeof stored;return ESP_OK;}
esp_err_t nvs_set_blob(nvs_handle_t n,const char *key,const void *data,size_t bytes){assert(n==1&&!strcmp(key,"watchlist")&&bytes==sizeof pending);++writes;if(fail_write)return ESP_FAIL;memcpy(pending,data,bytes);return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t n){assert(n==1);if(fail_commit)return ESP_FAIL;memcpy(stored,pending,sizeof stored);exists=true;return ESP_OK;}
void nvs_close(nvs_handle_t n){assert(n==1);++closes;}
int main(int argc,char **argv) {
    char normalized[STOCK_SYMBOL_BYTES],json[STOCK_HTTP_BYTES];stock_quote_t quote;
    assert(stock_symbol_normalize(" SH600519 ",normalized)&&!strcmp(normalized,"sh600519"));
    assert(stock_symbol_normalize("usaapl",normalized)&&!strcmp(normalized,"usAAPL"));
    const char *bad[]={"000001","sh1","hk700","sh600519&code=usAAPL","usAAPL/","us","us012","bj920001","sh600519\n","usBRK.B"};
    for(unsigned i=0;i<sizeof bad/sizeof bad[0];++i)assert(!stock_symbol_normalize(bad[i],normalized));
    const char *symbols[]={"sh600519","hk00700","usAAPL"};
    const char *currencies[]={"CNY","HKD","USD"};
    for(unsigned i=0;i<3;++i){size_t bytes=stock_fixture(symbols[i],json);assert(stock_quote_parse(json,bytes,symbols[i],&quote));assert(!strcmp(quote.symbol,symbols[i]));assert(strlen(quote.timestamp)==19);assert(quote.name[0]&&quote.price[0]&&!strcmp(quote.currency,currencies[i]));}
    size_t rmb_bytes=stock_fixture("hk80700",json);assert(stock_quote_parse(json,rmb_bytes,"hk80700",&quote)&&!strcmp(quote.currency,"CNY"));
    stock_fixture("sh600519",json);cJSON *root=cJSON_Parse(json);assert(root);
    cJSON *data=cJSON_GetObjectItem(root,"data"),*entry=cJSON_GetObjectItem(data,"sh600519");
    cJSON *row=cJSON_GetObjectItem(cJSON_GetObjectItem(entry,"qt"),"sh600519");
    const struct {int field;const char *text;} invalid[]={{0,"100"},{1,""},{1,"\xc0\xaf"},{2,"600518"},{3,"0"},{3,"NaN"},{3,"1e3"},{3,"12.3x"},{30,"20260230142032"},{30,"20261103992032"},{30,"202611031420"},{31,"oops"},{32,"Infinity"},{32,"-99"},{82,"XYZ"}};
    for(unsigned i=0;i<sizeof invalid/sizeof invalid[0];++i){
        cJSON *old=cJSON_Duplicate(cJSON_GetArrayItem(row,invalid[i].field),true);assert(old);
        cJSON_ReplaceItemInArray(row,invalid[i].field,cJSON_CreateString(invalid[i].text));char *text=cJSON_PrintUnformatted(root);assert(text);
        memset(&quote,0x5a,sizeof quote);stock_quote_t before=quote;assert(!stock_quote_parse(text,strlen(text),"sh600519",&quote));assert(!memcmp(&before,&quote,sizeof quote));free(text);
        cJSON_ReplaceItemInArray(row,invalid[i].field,old);
    }
    cJSON_Delete(root);assert(!stock_quote_parse(json,strlen(json),"sz000001",&quote));assert(!stock_quote_parse("{}",2,"sh600519",&quote));
    stock_list_t list={0},out={0};assert(stock_list_load(&out)&&!out.count&&!writes);
    list.count=3;for(unsigned i=0;i<3;++i)strcpy(list.symbols[i],symbols[i]);assert(stock_list_save(&list));assert(stock_list_load(&out)&&out.count==3&&!strcmp(out.symbols[2],"usAAPL"));
    uint8_t original[sizeof stored];memcpy(original,stored,sizeof original);fail_commit=true;list.count=2;assert(!stock_list_save(&list));assert(!memcmp(original,stored,sizeof original));fail_commit=false;
    fail_write=true;assert(!stock_list_save(&list));fail_write=false;assert(!memcmp(original,stored,sizeof original));
    fail_read=true;out.count=7;assert(!stock_list_load(&out)&&out.count==7);fail_read=false;
    stored[0]=99;assert(!stock_list_load(&out));stored[0]=1;stored[1]=9;assert(!stock_list_load(&out));memcpy(stored,original,sizeof stored);
    list.count=2;strcpy(list.symbols[1],list.symbols[0]);unsigned before=writes;assert(!stock_list_save(&list)&&writes==before);assert(opens==closes);
    if(argc==3){FILE *f=fopen(argv[1],"rb");assert(f);size_t bytes=fread(json,1,sizeof json-1,f);fclose(f);json[bytes]=0;assert(stock_quote_parse(json,bytes,argv[2],&quote));printf("Live parser accepted %s %s\n",quote.symbol,quote.timestamp);}
    puts("Stock symbol, quote validation and isolated persistence passed");return 0;
}
