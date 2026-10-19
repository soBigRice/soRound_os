#include "capsule_store.h"
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#ifndef CAPSULE_STORE_HOST
#include "esp_partition.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"
#include "esp_rom_crc.h"
#else
#include <sys/statvfs.h>
#endif

#define CAPSULE_ADDRESS 0x1801000u
#define CAPSULE_SIZE 0x7ff000u
#define CAPSULE_SECTOR 4096u
#ifndef CAPSULE_ROOT
#define CAPSULE_ROOT "/caps"
#endif
#define TEMP_PATH CAPSULE_ROOT "/RECORD.TMP"
static bool s_mounted,s_recovered;
static FILE *s_file;
static uint32_t s_bytes,s_budget;
static char s_final[CAPSULE_PATH_BYTES];
#ifndef CAPSULE_STORE_HOST
static wl_handle_t s_wl=WL_INVALID_HANDLE;
#endif

static uint32_t guard_crc(const uint8_t *p,size_t n) {
    // 与 SDK esp_rom_crc32_le(UINT32_MAX) 一致。 / Same seed and final XOR as the SDK WL generator.
    uint32_t c=0;for(size_t i=0;i<n;++i){c^=p[i];for(unsigned b=0;b<8;++b)c=(c>>1)^((c&1)?0xedb88320u:0);}
    return ~c;
}
bool capsule_store_meta_valid(const uint8_t m[32]) {
    return !memcmp(m,"GTVCAP01",8) && capsule_le32(m+8)==1 && capsule_le32(m+12)==CAPSULE_ADDRESS &&
        capsule_le32(m+16)==CAPSULE_SIZE && capsule_le32(m+20)==CAPSULE_SECTOR &&
        capsule_le32(m+24)==2 && capsule_le32(m+28)==guard_crc(m,28);
}
bool capsule_store_wl_state_valid(const uint8_t h[64]) {
    unsigned sectors=CAPSULE_SIZE/CAPSULE_SECTOR;
    unsigned state_sectors=(64+sectors*16+CAPSULE_SECTOR-1)/CAPSULE_SECTOR;
    unsigned max=sectors-1-2*state_sectors;
    return capsule_le32(h+60)==guard_crc(h,60) && capsule_le32(h+24)==2 &&
        capsule_le32(h+20)==CAPSULE_SECTOR && capsule_le32(h+4)==max &&
        capsule_le32(h)<max && capsule_le32(h+16)==16;
}

capsule_result_t capsule_store_mount(void) {
    if(s_mounted)return CAPSULE_OK;
#ifndef CAPSULE_STORE_HOST
    const esp_partition_t *meta=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"capsule_meta");
    const esp_partition_t *part=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_FAT,"capsules");
    if(!meta || !part || meta->address!=0x1800000 || meta->size!=CAPSULE_SECTOR ||
       part->address!=CAPSULE_ADDRESS || part->size!=CAPSULE_SIZE || part->encrypted)return CAPSULE_UNCONFIGURED;
    uint8_t header[32],state[64];
    if(esp_partition_read(meta,0,header,sizeof header)!=ESP_OK || !capsule_store_meta_valid(header))return CAPSULE_UNCONFIGURED;
    unsigned sectors=CAPSULE_SIZE/CAPSULE_SECTOR;
    size_t state_size=((64+sectors*16+CAPSULE_SECTOR-1)/CAPSULE_SECTOR)*CAPSULE_SECTOR;
    for(unsigned copy=1;copy<=2;++copy) {
        size_t offset=CAPSULE_SIZE-CAPSULE_SECTOR-copy*state_size;
        if(esp_partition_read(part,offset,state,sizeof state)!=ESP_OK || !capsule_store_wl_state_valid(state))
            return CAPSULE_CORRUPT_STORAGE;
    }
    // 只有离线初始化且WL状态有效才可挂载；绝不自动格式化。
    // Mount only an offline-prepared image with valid WL state; never format on failure.
    esp_vfs_fat_mount_config_t cfg={.max_files=3,.format_if_mount_failed=false,.allocation_unit_size=4096};
    if(esp_vfs_fat_spiflash_mount_rw_wl(CAPSULE_ROOT,"capsules",&cfg,&s_wl)!=ESP_OK) {
        if(s_wl!=WL_INVALID_HANDLE){wl_unmount(s_wl);s_wl=WL_INVALID_HANDLE;}
        return CAPSULE_CORRUPT_STORAGE;
    }
#endif
    s_mounted=true;
    // 未完成录音只有临时名；清理不完整文件，不把它冒充已保存。
    // Interrupted recordings retain a temporary name and never appear as saved clips.
    struct stat st;
    if(!stat(TEMP_PATH,&st)){if(unlink(TEMP_PATH))return CAPSULE_IO_ERROR;s_recovered=true;}
    return CAPSULE_OK;
}
void capsule_store_unmount(void) {
    if(s_file)(void)capsule_store_finish(false);
#ifndef CAPSULE_STORE_HOST
    if(s_mounted)esp_vfs_fat_spiflash_unmount_rw_wl(CAPSULE_ROOT,s_wl);
    s_wl=WL_INVALID_HANDLE;
#endif
    s_mounted=false;
}
bool capsule_store_path(const char *name,char path[CAPSULE_PATH_BYTES]) {
    if(!capsule_name_valid(name))return false;
    return snprintf(path,CAPSULE_PATH_BYTES,"%s/%s",CAPSULE_ROOT,name)<(int)CAPSULE_PATH_BYTES;
}
static int newest(const void *a,const void *b) {
    return strcmp(((const capsule_entry_t *)b)->name,((const capsule_entry_t *)a)->name);
}
capsule_result_t capsule_store_list(capsule_catalog_t *catalog) {
    if(!catalog || !s_mounted)return CAPSULE_UNCONFIGURED;
    memset(catalog,0,sizeof *catalog);catalog->recovered=s_recovered;
#ifndef CAPSULE_STORE_HOST
    uint64_t total=0,free=0;
    if(esp_vfs_fat_info(CAPSULE_ROOT,&total,&free)!=ESP_OK)return CAPSULE_IO_ERROR;
    catalog->free_bytes=(uint32_t)(free>UINT32_MAX?UINT32_MAX:free);
#else
    struct statvfs space;if(statvfs(CAPSULE_ROOT,&space))return CAPSULE_IO_ERROR;
    uint64_t free=(uint64_t)space.f_bavail*space.f_frsize;
    catalog->free_bytes=(uint32_t)(free>UINT32_MAX?UINT32_MAX:free);
#endif
    DIR *d=opendir(CAPSULE_ROOT);if(!d)return CAPSULE_IO_ERROR;
    struct dirent *de;
    while((de=readdir(d))) {
        if(!capsule_name_valid(de->d_name))continue;
        if(catalog->count==CAPSULE_MAX_FILES){closedir(d);return CAPSULE_FULL;}
        capsule_entry_t *e=&catalog->entries[catalog->count++];memcpy(e->name,de->d_name,13);
        char path[CAPSULE_PATH_BYTES];capsule_store_path(e->name,path);struct stat st;
        FILE *f=fopen(path,"rb");e->valid=f && capsule_wav_open(f,&e->bytes);if(f)fclose(f);
        if(!stat(path,&st))e->created=st.st_mtime;
        if(!e->valid)catalog->damaged++;
    }
    closedir(d);qsort(catalog->entries,catalog->count,sizeof catalog->entries[0],newest);return CAPSULE_OK;
}
capsule_result_t capsule_store_begin(void) {
    if(!s_mounted)return CAPSULE_UNCONFIGURED;
    if(s_file)return CAPSULE_INVALID;
    capsule_catalog_t catalog;capsule_result_t r=capsule_store_list(&catalog);if(r!=CAPSULE_OK)return r;
    if(catalog.count>=CAPSULE_MAX_FILES || catalog.free_bytes<CAPSULE_MIN_FREE+4096)return CAPSULE_FULL;
    time_t now=time(NULL);uint32_t id=now>0?(uint32_t)now:1;if(catalog.count){uint32_t last=(uint32_t)strtoul(catalog.entries[0].name,NULL,16);if(id<=last)id=last+1;}
    if(!id)return CAPSULE_FULL;
    char name[13];snprintf(name,sizeof name,"%08lX.WAV",(unsigned long)id);capsule_store_path(name,s_final);
    s_budget=catalog.free_bytes-CAPSULE_MIN_FREE-44;if(s_budget>CAPSULE_MAX_BYTES)s_budget=CAPSULE_MAX_BYTES;s_budget&=~1u;
    s_file=fopen(TEMP_PATH,"wb");if(!s_file)return errno==ENOSPC?CAPSULE_FULL:CAPSULE_IO_ERROR;
    uint8_t h[44];capsule_wav_header(h,0);s_bytes=0;
    if(fwrite(h,1,44,s_file)!=44){(void)capsule_store_finish(false);return CAPSULE_IO_ERROR;}
    return CAPSULE_OK;
}
capsule_result_t capsule_store_write(const int16_t *pcm,size_t samples) {
    if(!s_file || !pcm || samples>512)return CAPSULE_INVALID;
    size_t bytes=samples*2;
    if(bytes>s_budget-s_bytes)return CAPSULE_FULL;
    // 短写可能已留下不完整样本，不能作为可安全保存的容量停止。
    // A short write may leave partial samples; classify it as I/O failure, not a safe capacity stop.
    if(fwrite(pcm,1,bytes,s_file)!=bytes)return CAPSULE_IO_ERROR;
    s_bytes+=(uint32_t)bytes;return CAPSULE_OK;
}
capsule_result_t capsule_store_finish(bool save) {
    if(!s_file)return CAPSULE_INVALID;
    capsule_result_t r=CAPSULE_OK;
    if(save && s_bytes) {
        uint8_t h[44];capsule_wav_header(h,s_bytes);
        if(fseek(s_file,0,SEEK_SET) || fwrite(h,1,44,s_file)!=44 || fflush(s_file) || fsync(fileno(s_file)))r=CAPSULE_IO_ERROR;
    } else {save=false;if(!s_bytes)r=CAPSULE_EMPTY;}
    if(fclose(s_file))r=CAPSULE_IO_ERROR;
    s_file=NULL;
    if(save && r==CAPSULE_OK && rename(TEMP_PATH,s_final))r=CAPSULE_IO_ERROR;
    if(!save || r!=CAPSULE_OK)(void)unlink(TEMP_PATH);
    s_bytes=0;return r;
}
capsule_result_t capsule_store_delete(const char *name) {
    char path[CAPSULE_PATH_BYTES];if(!s_mounted)return CAPSULE_UNCONFIGURED;
    if(!capsule_store_path(name,path))return CAPSULE_INVALID;
    return unlink(path)?CAPSULE_IO_ERROR:CAPSULE_OK;
}
