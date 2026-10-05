#include "merit_store.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int open_error,get_error,set_error,commit_error,opened,closed,mode;
static uint32_t saved;
esp_err_t nvs_open(const char *name,int m,nvs_handle_t *h){assert(!strcmp(name,"merit"));if(open_error)return open_error;*h=7;mode=m;++opened;return ESP_OK;}
esp_err_t nvs_get_u32(nvs_handle_t h,const char *key,uint32_t *v){assert(h==7&&mode==NVS_READONLY&&!strcmp(key,"count"));if(get_error)return get_error;*v=saved;return ESP_OK;}
esp_err_t nvs_set_u32(nvs_handle_t h,const char *key,uint32_t v){assert(h==7&&mode==NVS_READWRITE&&!strcmp(key,"count"));if(set_error)return set_error;saved=v;return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h){assert(h==7);return commit_error;}
void nvs_close(nvs_handle_t h){assert(h==7);++closed;}
int main(void){uint32_t out=99;
 open_error=ESP_ERR_NVS_NOT_FOUND;assert(merit_load(&out)&&out==0);open_error=ESP_FAIL;out=99;assert(!merit_load(&out)&&out==99);open_error=0;
 get_error=ESP_ERR_NVS_NOT_FOUND;assert(merit_load(&out)&&out==0);get_error=ESP_FAIL;out=99;assert(!merit_load(&out)&&out==99);get_error=0;
 saved=MERIT_MAX+1;assert(!merit_load(&out)&&out==99);assert(!merit_save(MERIT_MAX+1));
 assert(merit_save(MERIT_MAX)&&merit_load(&out)&&out==MERIT_MAX);
 set_error=ESP_FAIL;assert(!merit_save(42));set_error=0;commit_error=ESP_FAIL;assert(!merit_save(42));commit_error=0;
 assert(merit_save(42)&&merit_load(&out)&&out==42);assert(opened==closed);puts("merit persistence: isolated namespace, missing/corrupt/error reads, bounds, commit failure and handle cleanup passed");}
