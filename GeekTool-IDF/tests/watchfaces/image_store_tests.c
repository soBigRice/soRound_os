// Actual cache/lifecycle controller; tasks and JPEG codec are deterministic service fixtures.
#include "img_store.h"
#include "watchface_backgrounds.h"
#include "jpeg_decoder.h"
#include "esp_vfs_fat.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool custom,task_fail,allocation_fail,decode_fail,wrong_size;
static int created,finished,head,tail;
static struct {void (*callback)(void *);void *arg;} jobs[16];
static const uint8_t tags[3]={0,1,2};
const watchface_jpeg_t watchface_backgrounds[3]={{tags,1},{tags+1,1},{tags+2,1}};
static FILE *image_open(const char *path,const char *mode){
    assert(!strcmp(path,"/img/bg.jpg")&&!strcmp(mode,"rb"));
    if(!custom)return NULL;
    FILE *f=tmpfile();assert(f);assert(fwrite("xx",1,2,f)==2);rewind(f);return f;
}
#define fopen(path,mode) image_open(path,mode)
#include "../../main/img_store.c"
#undef fopen
int esp_vfs_fat_spiflash_mount_ro(const char *base,const char *partition,const esp_vfs_fat_mount_config_t *cfg){
    assert(!strcmp(base,"/img")&&!strcmp(partition,"storage")&&!cfg->format_if_mount_failed);return ESP_OK;
}
void *heap_caps_malloc(size_t size,unsigned flags){assert(flags==MALLOC_CAP_SPIRAM);return allocation_fail?NULL:malloc(size);}
int xTaskCreate(void (*callback)(void *),const char *name,uint32_t stack,void *arg,unsigned priority,void *handle){
    (void)name;(void)priority;(void)handle;assert(stack==4096);
    if(task_fail)return 0;
    assert(tail<16);jobs[tail].callback=callback;jobs[tail++].arg=arg;++created;return pdPASS;
}
void vTaskDelete(void *handle){assert(!handle);++finished;}
int esp_jpeg_decode(esp_jpeg_image_cfg_t *cfg,esp_jpeg_image_output_t *info){
    assert(cfg->outbuf_size==466*466*2&&cfg->flags.swap_color_bytes==0);
    info->width=wrong_size?465:466;info->height=466;
    memset(cfg->outbuf,cfg->indata[0],cfg->outbuf_size);return decode_fail?-1:ESP_OK;
}
static void drain(void){while(head<tail){int i=head++;jobs[i].callback(jobs[i].arg);}}
static void reset(void){
    assert(head==tail&&finished==created);
    free((void *)s_dsc.data);s_dsc=(lv_image_dsc_t){0};
    for(int i=0;i<3;++i){free((void *)s_defaults[i].data);s_defaults[i]=(lv_image_dsc_t){0};}
    memset(s_default_started,0,sizeof s_default_started);memset(s_default_done,0,sizeof s_default_done);memset(s_default_ok,0,sizeof s_default_ok);
    s_started=s_done=s_ok=custom=task_fail=allocation_fail=decode_fail=wrong_size=false;head=tail=created=finished=0;
}
int main(void){
    assert(!img_store_face_image_for(-1)&&!img_store_face_loading(3));
    assert(!img_store_face_image_for(0)&&created==1&&img_store_face_loading(0));drain();
    for(int i=0;i<3;++i){assert(!img_store_face_image_for(i));for(int j=0;j<5;++j)assert(!img_store_face_image_for(i));assert(created==i+2);}
    drain();for(int i=0;i<3;++i){const lv_image_dsc_t *d=img_store_face_image_for(i);assert(d&&d->header.w==466&&d->header.h==466&&d->data_size==466*466*2&&!img_store_face_loading(i));}
    assert(created==4&&finished==4);reset();
    custom=true;assert(!img_store_face_image_for(2));drain();
    const lv_image_dsc_t *d=img_store_face_image_for(2);assert(d);
    for(int i=0;i<3;++i)assert(img_store_face_image_for(i)==d&&!s_default_started[i]);assert(created==1);reset();
    assert(!img_store_face_image_for(0));drain();task_fail=true;assert(!img_store_face_image_for(0)&&!img_store_face_loading(0));reset();
    assert(!img_store_face_image_for(0));drain();allocation_fail=true;assert(!img_store_face_image_for(0));drain();assert(!img_store_face_image_for(0)&&!img_store_face_loading(0));reset();
    assert(!img_store_face_image_for(0));drain();decode_fail=true;assert(!img_store_face_image_for(0));drain();assert(!s_defaults[0].data&&!img_store_face_loading(0));reset();
    assert(!img_store_face_image_for(0));drain();wrong_size=true;assert(!img_store_face_image_for(0));drain();assert(!s_defaults[0].data&&!img_store_face_loading(0));reset();
    puts("Default JPEG cache: custom priority, one task per theme, bounded buffers, allocation/task/decode/dimension failures and completed task cleanup passed.");return 0;
}
