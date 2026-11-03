#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#define MINIZ_NO_ZLIB_APIS
static unsigned live,files,calls,fail_at;
static void *probe_malloc(size_t size){++calls;if(fail_at&&calls==fail_at)return NULL;void *p=malloc(size);if(p)++live;return p;}
static void probe_free(void *p){if(p){assert(live);--live;}free(p);}
static FILE *probe_fopen(const char *path,const char *mode){FILE *f=fopen(path,mode);if(f)++files;return f;}
static int probe_fclose(FILE *f){assert(files);--files;return fclose(f);}
#define malloc probe_malloc
#define free probe_free
#define fopen probe_fopen
#define fclose probe_fclose
#include "../../main/mimi_assets.c"
#undef malloc
#undef free
#undef fopen
#undef fclose

void assets_probe_fail(unsigned nth){assert(!live&&!files);calls=0;fail_at=nth;}
unsigned assets_probe_live(void){return live;}
unsigned assets_probe_files(void){return files;}
unsigned assets_probe_calls(void){return calls;}
size_t assets_probe_slot_size(void){return sizeof(mimi_asset_slot_t);}
size_t assets_probe_frame_size(void){return sizeof(mimi_asset_frame_t);}
#if defined(ESP_PLATFORM) && !defined(MIMI_ASSET_REAL_TINFL)
// 本构建仅验证ESP第三条分配失败；真实ROM解压须在设备检查，不能用stub冒充。
// This build checks the third ESP allocation failure only; it does not pretend to validate the ROM inflater.
tinfl_status tinfl_decompress(tinfl_decompressor *r,const mz_uint8 *in,size_t *input,mz_uint8 *start,mz_uint8 *out,size_t *output,mz_uint32 flags){
    (void)r;(void)in;(void)input;(void)start;(void)out;(void)output;(void)flags;return TINFL_STATUS_FAILED;
}
#endif

#ifdef MIMI_ASSET_PROBE_MAIN
#include ASSET_EXPECTED_HEADER
typedef struct {uint64_t before[2];mimi_asset_slot_t slot;uint64_t after[2];} guarded_slot_t;
int main(int argc,char **argv){
    assert(argc==2);
#ifdef ESP_PLATFORM
    const unsigned allocations=3;
#else
    const unsigned allocations=2;
#endif
    for(unsigned fail=1;fail<=allocations;fail++){assets_probe_fail(fail);assert(!mimi_assets_open(argv[1]));assert(!live&&!files);}
    assets_probe_fail(0);mimi_assets_t *a=mimi_assets_open(argv[1]);assert(a&&live==allocations&&files==1);
    guarded_slot_t *g=malloc(sizeof *g);assert(g);g->before[0]=g->before[1]=UINT64_C(0x2468fedc1357ab90);g->after[0]=g->after[1]=UINT64_C(0x9876543210abcdef);
    unsigned rows=0;
    for(unsigned frame=0;frame<MIMI_ASSET_FRAME_COUNT;frame++){
        assert(mimi_assets_load(a,frame,&g->slot));uint32_t crc=0;
        for(unsigned y=0;y<MIMI_ASSET_SIDE;y++){
            uint8_t row[MIMI_ASSET_SIDE+2];memset(row,0xa7,sizeof row);
            assert(mimi_assets_body_row(&g->slot.frame,y,row+1));assert(row[0]==0xa7&&row[MIMI_ASSET_SIDE+1]==0xa7);
            crc=(uint32_t)crc32(crc,row+1,MIMI_ASSET_SIDE);++rows;
        }
        assert(crc==expected_body_crc[frame]);
        assert(calls==allocations&&live==allocations&&files==1);
        assert(g->before[0]==UINT64_C(0x2468fedc1357ab90)&&g->before[1]==g->before[0]);
        assert(g->after[0]==UINT64_C(0x9876543210abcdef)&&g->after[1]==g->after[0]);
    }
    unsigned index=77;uint32_t pose=77;
    assert(!mimi_assets_find(a,0,0,&index,&pose)&&index==77&&pose==77);
    assert(mimi_assets_find(a,10,UINT32_MAX,&index,&pose)&&index==49&&pose==2450);
    assert(mimi_assets_find(a,11,50,&index,&pose)&&index==51&&pose==50);
    assert(mimi_assets_find(a,13,UINT32_MAX,&index,&pose)&&index==185&&pose==2850);
    assert(!mimi_assets_load(a,MIMI_ASSET_FRAME_COUNT,&g->slot));assert(!mimi_assets_load(a,0,NULL));assert(!mimi_assets_load(NULL,0,&g->slot));
    uint8_t row[MIMI_ASSET_SIDE];memset(row,0xa7,sizeof row);
    assert(!mimi_assets_body_row(&g->slot.frame,MIMI_ASSET_SIDE,row));for(unsigned x=0;x<sizeof row;x++)assert(row[x]==0xa7);
    assert(!mimi_assets_body_row(NULL,0,row));assert(!mimi_assets_body_row(&g->slot.frame,0,NULL));
    free(g);mimi_assets_close(a);mimi_assets_close(NULL);assert(!live&&!files);
    printf("reader_frames=%u body_rows=%u heap=0 files=0 bounds=true\n",MIMI_ASSET_FRAME_COUNT,rows);
    return 0;
}
#endif
