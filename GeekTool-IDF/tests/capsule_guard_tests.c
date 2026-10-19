#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../main/capsule_store.c"
static esp_partition_t meta={.address=0x1800000,.size=4096},data={.address=CAPSULE_ADDRESS,.size=CAPSULE_SIZE};
static uint8_t guard[32],state_header[64];
static unsigned scenario,mount_calls,unmount_calls,read_calls;
const esp_partition_t *esp_partition_find_first(int type,int subtype,const char *name) {
    (void)subtype;assert(type==ESP_PARTITION_TYPE_DATA);
    // This production mount path must never even ask for the existing image partition.
    // 生产挂载路径绝不查找或访问原表盘资源分区。
    assert(!strcmp(name,"capsule_meta") || !strcmp(name,"capsules"));
    if(scenario==0 && !strcmp(name,"capsule_meta"))return NULL;
    return !strcmp(name,"capsule_meta")?&meta:&data;
}
int esp_partition_read(const esp_partition_t *part,size_t at,void *out,size_t n) {
    ++read_calls;
    if(part==&meta){assert(at==0&&n==32);memcpy(out,guard,32);if(scenario==1)((uint8_t *)out)[0]^=1;}
    else {assert(part==&data&&n==64&&at<CAPSULE_SIZE);memcpy(out,state_header,64);
        if((scenario==2 && read_calls==2)||(scenario==3 && read_calls==3))((uint8_t *)out)[0]^=1;}
    return ESP_OK;
}
int esp_vfs_fat_spiflash_mount_rw_wl(const char *base,const char *label,const esp_vfs_fat_mount_config_t *cfg,wl_handle_t *out) {
    assert(!strcmp(base,"/caps")&&!strcmp(label,"capsules")&&!cfg->format_if_mount_failed&&cfg->max_files==3&&cfg->allocation_unit_size==4096);
    ++mount_calls;*out=1;return scenario==5?-1:ESP_OK;
}
int esp_vfs_fat_spiflash_unmount_rw_wl(const char *base,wl_handle_t handle){assert(!strcmp(base,"/caps")&&handle==1);++unmount_calls;return ESP_OK;}
int wl_unmount(wl_handle_t handle){assert(handle==1);++unmount_calls;return ESP_OK;}
int esp_vfs_fat_info(const char *base,uint64_t *total,uint64_t *free){assert(!strcmp(base,"/caps"));*total=CAPSULE_SIZE;*free=7000000;return ESP_OK;}
int main(void) {
    memcpy(guard,"GTVCAP01",8);capsule_put32(guard+8,1);capsule_put32(guard+12,CAPSULE_ADDRESS);
    capsule_put32(guard+16,CAPSULE_SIZE);capsule_put32(guard+20,4096);capsule_put32(guard+24,2);capsule_put32(guard+28,guard_crc(guard,28));
    capsule_put32(state_header+4,2028);capsule_put32(state_header+16,16);capsule_put32(state_header+20,4096);capsule_put32(state_header+24,2);
    capsule_put32(state_header+60,guard_crc(state_header,60));
    for(scenario=0;scenario<4;++scenario){read_calls=0;assert(capsule_store_mount()!=CAPSULE_OK&&mount_calls==0);}
    scenario=4;data.address++;assert(capsule_store_mount()==CAPSULE_UNCONFIGURED&&mount_calls==0);data.address--;
    read_calls=0;assert(capsule_store_mount()==CAPSULE_OK&&mount_calls==1&&read_calls==3);capsule_store_unmount();assert(unmount_calls==1);
    scenario=5;read_calls=0;assert(capsule_store_mount()==CAPSULE_CORRUPT_STORAGE&&mount_calls==2&&unmount_calls==2);
    puts("capsule guard: actual mount path rejects missing partition, bad meta/address and either corrupt WL copy before RW call; original storage never accessed; formatting disabled and mount failure cleaned up");
}
