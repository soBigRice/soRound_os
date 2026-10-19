#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/statvfs.h>
static bool short_write;
static size_t test_write(const void *p,size_t size,size_t count,FILE *f) {
    if(short_write){short_write=false;return fwrite(p,size,count?count-1:0,f);}
    return fwrite(p,size,count,f);
}
#define CAPSULE_STORE_HOST 1
#define CAPSULE_ROOT "."
#define fwrite(p,s,n,f) test_write(p,s,n,f)
#include "../main/capsule_store.c"
#undef fwrite

static void guards(const char *dir) {
    if(!dir)return;
    char path[512];snprintf(path,sizeof path,"%s/capsule_meta.bin",dir);FILE *f=fopen(path,"rb");assert(f);
    uint8_t meta[32];assert(fread(meta,1,32,f)==32);fclose(f);assert(capsule_store_meta_valid(meta));
    meta[12]^=1;assert(!capsule_store_meta_valid(meta));meta[12]^=1;
    meta[28]^=1;assert(!capsule_store_meta_valid(meta));
    snprintf(path,sizeof path,"%s/capsules.bin",dir);f=fopen(path,"rb");assert(f);
    unsigned size=((64+(CAPSULE_SIZE/4096)*16+4095)/4096)*4096;
    for(unsigned i=1;i<=2;++i){uint8_t h[64];assert(!fseek(f,CAPSULE_SIZE-4096-i*size,SEEK_SET));assert(fread(h,1,64,f)==64);
        assert(capsule_store_wl_state_valid(h));h[24]=1;assert(!capsule_store_wl_state_valid(h));}
    fclose(f);
}
int main(int argc,char **argv) {
    guards(argc>1?argv[1]:NULL);
    uint8_t h[44];uint32_t n=0;assert(capsule_wav_header(h,32000));assert(capsule_wav_parse(h,32044,&n)&&n==32000);
    assert(!capsule_wav_parse(h,32043,&n));h[22]=2;assert(!capsule_wav_parse(h,32044,&n));
    assert(!capsule_wav_header(h,CAPSULE_MAX_BYTES+2));assert(!capsule_wav_header(h,3));
    assert(capsule_path_valid("/caps/1234ABCD.WAV"));assert(!capsule_path_valid("/img/1234ABCD.WAV"));
    assert(!capsule_path_valid("/caps/../x.WAV"));assert(!capsule_name_valid("RECORD.TMP"));
    char original[4096];assert(getcwd(original,sizeof original));char temp[]="/tmp/gt-capsules-XXXXXX";assert(mkdtemp(temp));assert(!chdir(temp));
    FILE *orphan=fopen("RECORD.TMP","wb");assert(orphan);fclose(orphan);
    assert(capsule_store_mount()==CAPSULE_OK);assert(access("RECORD.TMP",F_OK));
    capsule_catalog_t catalog;assert(capsule_store_list(&catalog)==CAPSULE_OK && catalog.recovered && !catalog.count);
    int16_t pcm[256];for(unsigned i=0;i<256;++i)pcm[i]=(int16_t)(i*31);
    assert(capsule_store_begin()==CAPSULE_OK);assert(capsule_store_write(pcm,256)==CAPSULE_OK);
    assert(capsule_store_finish(true)==CAPSULE_OK);assert(capsule_store_list(&catalog)==CAPSULE_OK && catalog.count==1 && catalog.entries[0].valid);
    char path[32];assert(capsule_store_path(catalog.entries[0].name,path));FILE *f=fopen(path,"rb");assert(f);
    assert(capsule_wav_open(f,&n)&&n==512);int16_t read[256];assert(fread(read,2,256,f)==256&&!memcmp(read,pcm,sizeof pcm));fclose(f);
    assert(capsule_store_begin()==CAPSULE_OK);assert(capsule_store_write(pcm,256)==CAPSULE_OK);assert(capsule_store_finish(false)==CAPSULE_OK);
    assert(capsule_store_list(&catalog)==CAPSULE_OK && catalog.count==1);
    assert(capsule_store_begin()==CAPSULE_OK);short_write=true;assert(capsule_store_write(pcm,256)==CAPSULE_IO_ERROR);(void)capsule_store_finish(false);
    assert(capsule_store_list(&catalog)==CAPSULE_OK && catalog.count==1 && access("RECORD.TMP",F_OK));
    assert(capsule_store_begin()==CAPSULE_OK);s_budget=2;assert(capsule_store_write(pcm,2)==CAPSULE_FULL);(void)capsule_store_finish(false);
    assert(capsule_store_delete("../bg.jpg")==CAPSULE_INVALID);assert(capsule_store_delete("RECORD.TMP")==CAPSULE_INVALID);
    for(unsigned i=1;i<CAPSULE_MAX_FILES;++i){assert(capsule_store_begin()==CAPSULE_OK);assert(capsule_store_write(pcm,1)==CAPSULE_OK);assert(capsule_store_finish(true)==CAPSULE_OK);}
    assert(capsule_store_begin()==CAPSULE_FULL);assert(capsule_store_list(&catalog)==CAPSULE_OK && catalog.count==CAPSULE_MAX_FILES);
    f=fopen(path,"wb");assert(f);fputs("truncated",f);fclose(f);assert(capsule_store_list(&catalog)==CAPSULE_OK && catalog.damaged==1);
    for(unsigned i=0;i<catalog.count;++i)assert(capsule_store_delete(catalog.entries[i].name)==CAPSULE_OK);
    capsule_store_unmount();assert(!chdir(original));assert(!rmdir(temp));
    puts("capsules: WAV integrity, actual PCM roundtrip, cancellation, short write, capacity/file limit, orphan cleanup, path guards, offline metadata/WL CRC passed");
}
