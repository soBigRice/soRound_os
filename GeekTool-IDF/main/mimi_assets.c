#include "mimi_assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_rom_crc.h"
#include "miniz.h"
#else
#include <zlib.h>
#endif

#define HEADER_BYTES 64u
#define INDEX_BYTES 64u
#define INPUT_BYTES 4096u
#define TABLE_BYTES ((MIMI_ASSET_SIDE+1u)*4u)
#define MAX_FILE_BYTES 0x7e0000u
#define MAX_PACKED_FRAME 96035u
#define MAX_SPAN_BYTES 409344u
typedef struct {
    uint32_t offset,raw[MIMI_ASSET_STREAMS],packed[MIMI_ASSET_STREAMS],crc;
    uint16_t pose;
    uint8_t clip;
} frame_index_t;
struct mimi_assets {
    FILE *file;
    uint32_t file_bytes;
    frame_index_t index[MIMI_ASSET_FRAME_COUNT];
    uint8_t *input;
#ifdef ESP_PLATFORM
    tinfl_decompressor *inflate;
#endif
};
_Static_assert(sizeof(mimi_asset_span_t)==8,"RGB565 span layout changed");
_Static_assert(sizeof(mimi_body_row_start_t)==4,"Body row index layout changed");

static void *allocate(size_t bytes,bool internal) {
#ifdef ESP_PLATFORM
    return heap_caps_malloc(bytes,MALLOC_CAP_8BIT|(internal?MALLOC_CAP_INTERNAL:MALLOC_CAP_SPIRAM));
#else
    (void)internal;return malloc(bytes);
#endif
}
static void release(void *memory) {
#ifdef ESP_PLATFORM
    heap_caps_free(memory);
#else
    free(memory);
#endif
}
static uint16_t u16(const uint8_t *p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static uint32_t u32(const uint8_t *p){return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint32_t checksum(uint32_t seed,const void *data,size_t bytes) {
#ifdef ESP_PLATFORM
    return esp_rom_crc32_le(seed,data,(uint32_t)bytes);
#else
    return (uint32_t)crc32(seed,data,(uInt)bytes);
#endif
}
static bool zero(const uint8_t *p,size_t bytes){for(size_t i=0;i<bytes;i++)if(p[i])return false;return true;}
static uint32_t align4(uint32_t value){return (value+3u)&~3u;}

void mimi_assets_close(mimi_assets_t *a) {
    if(!a)return;
    if(a->file)fclose(a->file);
    release(a->input);
#ifdef ESP_PLATFORM
    release(a->inflate);
#endif
    release(a);
}
mimi_assets_t *mimi_assets_open(const char *path) {
    if(!path)return NULL;
    mimi_assets_t *a=allocate(sizeof *a,false);if(!a)return NULL;memset(a,0,sizeof *a);
    a->file=fopen(path,"rb");if(!a->file)goto bad;
    if(fseek(a->file,0,SEEK_END))goto bad;
    long length=ftell(a->file);
    if(length<(long)(HEADER_BYTES+MIMI_ASSET_FRAME_COUNT*INDEX_BYTES)||length>MAX_FILE_BYTES||fseek(a->file,0,SEEK_SET))goto bad;
    uint8_t header[HEADER_BYTES];if(fread(header,1,sizeof header,a->file)!=sizeof header)goto bad;
    if(memcmp(header,"MIMIPAK\0",8)||u32(header+8)!=MIMI_ASSET_VERSION||u32(header+12)!=HEADER_BYTES||
       u32(header+16)!=MIMI_ASSET_FRAME_COUNT||u16(header+20)!=MIMI_ASSET_SIDE||u16(header+22)!=MIMI_ASSET_SIDE||
       u16(header+24)!=50||u16(header+26)!=MIMI_ASSET_STREAMS||u32(header+28)!=MIMI_ASSET_FRAME_COUNT*INDEX_BYTES||
       u32(header+32)!=(uint32_t)length||u32(header+40)!=1||!zero(header+44,16)||
       checksum(0,header,60)!=u32(header+60))goto bad;
    a->file_bytes=(uint32_t)length;
    uint32_t index_crc=0,next=HEADER_BYTES+MIMI_ASSET_FRAME_COUNT*INDEX_BYTES;
    for(unsigned i=0;i<MIMI_ASSET_FRAME_COUNT;i++){
        uint8_t raw[INDEX_BYTES];if(fread(raw,1,sizeof raw,a->file)!=sizeof raw)goto bad;
        index_crc=checksum(index_crc,raw,sizeof raw);frame_index_t *f=&a->index[i];
        unsigned start=i<50?0:(i<128?50:128),clip=i<50?10:(i<128?11:13);
        f->offset=u32(raw);f->pose=u16(raw+4);f->clip=raw[6];f->crc=u32(raw+48);
        if(f->offset!=next||f->pose!=(i-start)*50||f->clip!=clip||raw[7]||!zero(raw+52,12))goto bad;
        uint64_t packed=0,unpacked=0;
        for(unsigned stream=0;stream<MIMI_ASSET_STREAMS;stream++){
            f->raw[stream]=u32(raw+8+stream*4);f->packed[stream]=u32(raw+28+stream*4);
            if(!f->packed[stream]||f->raw[stream]>MIMI_ASSET_FRAME_BYTES)goto bad;
            packed+=f->packed[stream];unpacked+=align4(f->raw[stream]);
        }
        if(f->raw[MIMI_ASSET_BASE_ROWS]!=TABLE_BYTES||f->raw[MIMI_ASSET_SPAN_ROWS]!=TABLE_BYTES||
           f->raw[MIMI_ASSET_BODY_RLE]<3||f->raw[MIMI_ASSET_BODY_RLE]>UINT16_MAX||
           !f->raw[MIMI_ASSET_BASE_RLE]||f->raw[MIMI_ASSET_SPANS]>MAX_SPAN_BYTES||f->raw[MIMI_ASSET_SPANS]%8||
           (!f->raw[MIMI_ASSET_SPANS]&&f->packed[MIMI_ASSET_SPANS]!=2)||
           unpacked>MIMI_ASSET_FRAME_BYTES||packed>MAX_PACKED_FRAME||f->offset+(uint64_t)align4((uint32_t)packed)>a->file_bytes)goto bad;
        next=f->offset+align4((uint32_t)packed);
    }
    if(next!=a->file_bytes||index_crc!=u32(header+36))goto bad;
    a->input=allocate(INPUT_BYTES,true);if(!a->input)goto bad;
#ifdef ESP_PLATFORM
    // ROM状态约11KiB，显式放内部堆，禁止便捷接口把它压到工作线程栈。
    // Keep the roughly11KiB ROM inflater state on the internal heap, never on the worker stack.
    a->inflate=allocate(sizeof *a->inflate,true);if(!a->inflate)goto bad;
#endif
    return a;
bad:mimi_assets_close(a);return NULL;
}

bool mimi_assets_find(const mimi_assets_t *a,unsigned clip,uint32_t ms,unsigned *index,uint32_t *pose) {
    if(!a||!index||!pose)return false;
    unsigned first,count;
    if(clip==10){first=0;count=50;}else if(clip==11){first=50;count=78;}else if(clip==13){first=128;count=58;}else return false;
    unsigned frame=ms/50;if(frame>=count)frame=count-1;
    *index=first+frame;*pose=a->index[*index].pose;return true;
}

// 一个worker串行复用输入及状态；每条流必须完整消耗、精确输出，不接受尾随数据。
// One worker serially reuses input/state; require exact output and complete input consumption with no trailing data.
static bool inflate_stream(mimi_assets_t *a,uint32_t packed,uint8_t *output,uint32_t raw) {
    if(!raw){uint8_t empty[2];return packed==2&&fread(empty,1,2,a->file)==2&&empty[0]==3&&!empty[1];}
    uint32_t left=packed,produced=0;size_t available=0,at=0;
#ifdef ESP_PLATFORM
    tinfl_init(a->inflate);
#else
    z_stream z={0};if(inflateInit2(&z,-15)!=Z_OK)return false;
#endif
    bool ok=false;
    for(;;){
        if(at==available&&left){available=left>INPUT_BYTES?INPUT_BYTES:left;at=0;if(fread(a->input,1,available,a->file)!=available)break;left-=(uint32_t)available;}
        size_t input=available-at,out=raw-produced;
#ifdef ESP_PLATFORM
        uint32_t flags=TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF|(left?TINFL_FLAG_HAS_MORE_INPUT:0);
        tinfl_status status=tinfl_decompress(a->inflate,a->input+at,&input,output,output+produced,&out,flags);
        at+=input;produced+=(uint32_t)out;
        if(status==TINFL_STATUS_DONE){ok=produced==raw&&!left&&at==available;break;}
        if(status<0||status==TINFL_STATUS_HAS_MORE_OUTPUT||(!input&&!out))break;
#else
        uint8_t overflow;
        z.next_in=a->input+at;z.avail_in=(uInt)input;z.next_out=out?output+produced:&overflow;z.avail_out=(uInt)(out?out:1);
        int status=inflate(&z,Z_NO_FLUSH);input-=z.avail_in;
        if(!out&&z.avail_out!=1)break;
        if(out)out-=z.avail_out;
        at+=input;produced+=(uint32_t)out;
        if(status==Z_STREAM_END){ok=produced==raw&&!left&&at==available;break;}
        if(status!=Z_OK||(!input&&!out))break;
#endif
        if(!left&&at==available)break;
    }
#ifndef ESP_PLATFORM
    inflateEnd(&z);
#endif
    return ok;
}

static bool validate_rle(const uint8_t *bytes,size_t size,size_t units,unsigned unit) {
    size_t at=0,done=0;
    while(at<size&&done<units){
        if(size-at<2)return false;
        uint16_t token=u16(bytes+at);at+=2;
        size_t count=token&0x7fff;if(!count||count>units-done)return false;
        size_t input=(token&0x8000)?unit:count*unit;if(input>size-at)return false;
        at+=input;done+=count;
    }
    return at==size&&done==units;
}
static bool index_body(mimi_asset_slot_t *s) {
    const uint8_t *in=s->frame.body_rle;size_t size=s->frame.body_bytes,at=0,done=0;unsigned row=0;
    while(at<size&&done<(size_t)MIMI_ASSET_SIDE*MIMI_ASSET_SIDE){
        size_t offset=at;if(size-at<2)return false;uint16_t token=u16(in+at);at+=2;
        size_t count=token&0x7fff;if(!count||count>(size_t)MIMI_ASSET_SIDE*MIMI_ASSET_SIDE-done)return false;
        size_t input=(token&0x8000)?1:count;if(input>size-at)return false;
        while(row<MIMI_ASSET_SIDE&&(size_t)row*MIMI_ASSET_SIDE<done+count){
            size_t first=(size_t)row*MIMI_ASSET_SIDE;
            if(first<done||first-done>UINT16_MAX||offset>UINT16_MAX)return false;
            s->body_rows[row++]=(mimi_body_row_start_t){(uint16_t)offset,(uint16_t)(first-done)};
        }
        done+=count;at+=input;
    }
    return at==size&&done==(size_t)MIMI_ASSET_SIDE*MIMI_ASSET_SIDE&&row==MIMI_ASSET_SIDE;
}
bool mimi_assets_load(mimi_assets_t *a,unsigned index,mimi_asset_slot_t *s) {
    if(!a||!s||index>=MIMI_ASSET_FRAME_COUNT)return false;
    const frame_index_t *f=&a->index[index];if(fseek(a->file,(long)f->offset,SEEK_SET))return false;
    uint32_t crc=0,used=0,packed=0;uint8_t *dest[MIMI_ASSET_STREAMS];
    dest[MIMI_ASSET_BASE_ROWS]=(uint8_t*)s->base_rows;dest[MIMI_ASSET_SPAN_ROWS]=(uint8_t*)s->span_rows;
    for(unsigned stream=MIMI_ASSET_BODY_RLE;stream<MIMI_ASSET_STREAMS;stream++){
        if(f->raw[stream]>sizeof s->data-used)return false;
        dest[stream]=s->data+used;used+=align4(f->raw[stream]);
    }
    for(unsigned stream=0;stream<MIMI_ASSET_STREAMS;stream++){
        if(!inflate_stream(a,f->packed[stream],dest[stream],f->raw[stream]))return false;
        crc=checksum(crc,dest[stream],f->raw[stream]);packed+=f->packed[stream];
    }
    uint8_t padding[3];unsigned pad=align4(packed)-packed;
    if(crc!=f->crc||(pad&&(fread(padding,1,pad,a->file)!=pad||!zero(padding,pad))))return false;
    s->frame=(mimi_asset_frame_t){.index=index,.clip=f->clip,.pose_ms=f->pose,.base_rows=s->base_rows,.span_rows=s->span_rows,
        .body_rle=dest[MIMI_ASSET_BODY_RLE],.base_rle=dest[MIMI_ASSET_BASE_RLE],.span_data=dest[MIMI_ASSET_SPANS],
        .body_bytes=f->raw[MIMI_ASSET_BODY_RLE],.base_bytes=f->raw[MIMI_ASSET_BASE_RLE],
        .span_count=f->raw[MIMI_ASSET_SPANS]/8,.body_rows=s->body_rows};
    if(s->base_rows[0]||s->span_rows[0]||s->base_rows[MIMI_ASSET_SIDE]!=s->frame.base_bytes||
       s->span_rows[MIMI_ASSET_SIDE]!=s->frame.span_count||!index_body(s))return false;
    for(unsigned row=0;row<MIMI_ASSET_SIDE;row++){
        uint32_t base=s->base_rows[row],end=s->base_rows[row+1],first=s->span_rows[row],last=s->span_rows[row+1];
        if(base>=end||end>s->frame.base_bytes||end-base>MIMI_ASSET_MAX_BASE_ROW_BYTES||
           !validate_rle(s->frame.base_rle+base,end-base,MIMI_ASSET_SIDE,2)||first>last||last>s->frame.span_count||last-first>MIMI_ASSET_MAX_ROW_SPANS)return false;
        for(uint32_t i=first;i<last;i++){
            const uint8_t *span=s->frame.span_data+i*8;unsigned x=u16(span),n=u16(span+2);
            if(!n||x+n>MIMI_ASSET_SIDE||!span[6]||span[6]>32||span[7])return false;
        }
    }
    return true;
}
bool mimi_assets_body_row(const mimi_asset_frame_t *f,unsigned row,uint8_t out[MIMI_ASSET_SIDE]) {
    if(!f||!out||row>=MIMI_ASSET_SIDE||!f->body_rle||!f->body_rows)return false;
    const mimi_body_row_start_t *start=&f->body_rows[row];size_t first=(size_t)row*MIMI_ASSET_SIDE,last=first+MIMI_ASSET_SIDE;
    if(start->offset>=f->body_bytes||start->skip>first)return false;
    size_t at=start->offset,logical=first-start->skip,written=0;
    while(at<f->body_bytes&&logical<last){
        if(f->body_bytes-at<2)return false;
        uint16_t token=u16(f->body_rle+at);at+=2;
        size_t count=token&0x7fff;if(!count||count>(size_t)MIMI_ASSET_SIDE*MIMI_ASSET_SIDE-logical)return false;
        size_t bytes=(token&0x8000)?1:count;if(bytes>f->body_bytes-at)return false;
        size_t end=logical+count,left=logical>first?logical:first,right=end<last?end:last;
        if(end>first){if(token&0x8000)memset(out+left-first,f->body_rle[at],right-left);else memcpy(out+left-first,f->body_rle+at+left-logical,right-left);written+=right-left;}
        logical=end;at+=bytes;
    }
    return written==MIMI_ASSET_SIDE;
}
