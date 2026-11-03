#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MIMI_ASSET_SIDE 466
#define MIMI_ASSET_FRAME_COUNT 186
#define MIMI_ASSET_FRAME_BYTES 416800
#define MIMI_ASSET_MAX_ROW_SPANS 623
#define MIMI_ASSET_MAX_BASE_ROW_BYTES 936
#define MIMI_ASSET_VERSION 1
#ifndef MIMI_ASSET_PATH
#define MIMI_ASSET_PATH "/img/MIMI.PAK"
#endif

// 五条流均为原生小端数据；索引与正文独立校验，运行时禁止直接映射资源分区。
// Five streams use native little-endian data, with separate metadata and payload checks; never mmap the resource partition.
enum { MIMI_ASSET_BASE_ROWS, MIMI_ASSET_SPAN_ROWS, MIMI_ASSET_BODY_RLE,
       MIMI_ASSET_BASE_RLE, MIMI_ASSET_SPANS, MIMI_ASSET_STREAMS };
typedef struct { uint16_t offset,skip; } mimi_body_row_start_t;
typedef struct { uint16_t x,n,color; uint8_t q,pad; } mimi_asset_span_t;
typedef struct {
    uint32_t index,clip,pose_ms;
    const uint32_t *base_rows,*span_rows;
    const uint8_t *body_rle,*base_rle,*span_data;
    uint32_t body_bytes,base_bytes,span_count;
    const mimi_body_row_start_t *body_rows;
} mimi_asset_frame_t;
typedef struct {
    mimi_asset_frame_t frame;
    uint32_t base_rows[MIMI_ASSET_SIDE+1],span_rows[MIMI_ASSET_SIDE+1];
    mimi_body_row_start_t body_rows[MIMI_ASSET_SIDE];
    _Alignas(4) uint8_t data[MIMI_ASSET_FRAME_BYTES];
} mimi_asset_slot_t;
typedef struct mimi_assets mimi_assets_t;

// 打开/关闭由拥有者管理；find只读目录可与worker并行，load及FILE只允许一个worker使用。
// The owner opens/closes the reader; immutable lookup may overlap the worker, while load and FILE access have one worker owner.
mimi_assets_t *mimi_assets_open(const char *path);
void mimi_assets_close(mimi_assets_t *assets);
bool mimi_assets_find(const mimi_assets_t *assets,unsigned clip,uint32_t elapsed_ms,unsigned *index,uint32_t *pose_ms);
// 失败可能留下部分输出，调用方必须丢弃整个slot；成功后直到UI释放前不可修改。
// Failure may leave partial output: discard the entire slot; a successful slot stays immutable until the UI releases it.
bool mimi_assets_load(mimi_assets_t *assets,unsigned index,mimi_asset_slot_t *slot);
bool mimi_assets_body_row(const mimi_asset_frame_t *frame,unsigned row,uint8_t out[MIMI_ASSET_SIDE]);
