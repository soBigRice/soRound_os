#include "nvs.h"
#include <assert.h>
#include <string.h>
bool mock_nvs_fail;
static uint8_t stored[20],pending[20];
static bool exists;
esp_err_t nvs_open(const char *namespace,int mode,nvs_handle_t *n) {
    assert(strcmp(namespace,"weather")==0);(void)mode;*n=1;return mock_nvs_fail?ESP_FAIL:ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t n,const char *key,void *out,size_t *size) {
    (void)n;assert(strcmp(key,"locations")==0);
    if(!exists)return ESP_ERR_NOT_FOUND;
    assert(*size>=sizeof stored);memcpy(out,stored,sizeof stored);*size=sizeof stored;return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t n,const char *key,const void *value,size_t size) {
    (void)n;assert(strcmp(key,"locations")==0 && size==sizeof stored);
    memcpy(pending,value,size);return mock_nvs_fail?ESP_FAIL:ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t n) {(void)n;if(mock_nvs_fail)return ESP_FAIL;memcpy(stored,pending,sizeof stored);exists=true;return ESP_OK;}
void nvs_close(nvs_handle_t n) {(void)n;}
