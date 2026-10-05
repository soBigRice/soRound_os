#include "merit_store.h"
#include "nvs.h"
#include "esp_log.h"

bool merit_load(uint32_t *out) {
    if(!out)return false;
    nvs_handle_t handle;uint32_t count=0;
    esp_err_t err=nvs_open("merit",NVS_READONLY,&handle);
    if(err==ESP_OK) {
        err=nvs_get_u32(handle,"count",&count);nvs_close(handle);
    }
    if(err==ESP_ERR_NVS_NOT_FOUND){*out=0;return true;}
    if(err!=ESP_OK || count>MERIT_MAX)return false;
    *out=count;return true;
}
bool merit_save(uint32_t count) {
    if(count>MERIT_MAX)return false;
    nvs_handle_t handle;esp_err_t err=nvs_open("merit",NVS_READWRITE,&handle);
    if(err==ESP_OK) {
        err=nvs_set_u32(handle,"count",count);
        if(err==ESP_OK)err=nvs_commit(handle);
        nvs_close(handle);
    }
    if(err!=ESP_OK)ESP_LOGW("merit","save failed: %d",err);
    return err==ESP_OK;
}
