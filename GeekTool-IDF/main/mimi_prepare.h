#pragma once
#include "mimi_pet.h"
#include <stdbool.h>
#include <stdint.h>
#define MIMI_PREPARE_SIDE 466
#define MIMI_PREPARE_EYE_SIZE 208
typedef struct {
    int16_t x,y;
    uint16_t left,top,width,height;
} mimi_prepared_eye_bounds_t;
typedef struct {
    uint8_t alpha[2][MIMI_PREPARE_EYE_SIZE*MIMI_PREPARE_EYE_SIZE];
    mimi_prepared_eye_bounds_t eye[2];
} mimi_prepared_eyes_t;
typedef bool (*mimi_prepare_body_row_fn)(void *context,unsigned y,uint8_t row[MIMI_PREPARE_SIDE]);
// 行提供者只在裁切需要时调用，须填满466字节；false可能留下部分输出，禁止发布。
// The provider fills466bytes only when clipping needs a row; false can leave partial output that must not be published.
// A8覆盖率与裁切在任务栈的内部行完成，每行只向输出缓冲复制一次。
// Coverage and clipping use internal task-stack rows, followed by one bulk copy to the output per row.
bool mimi_prepare_eyes_rows(const mimi_pet_t *pet,uint32_t pose_ms,mimi_prepare_body_row_fn read_body,void *context,
                            int16_t delta[MIMI_PREPARE_SIDE+1],mimi_prepared_eyes_t *out);
// 单个调用方独占out/delta，pet/body为不可变快照；body须有466*466字节。
// One caller owns out/delta; pet/body are immutable snapshots with466*466bodybytes.
// delta须有467个int16_t，工作线程热路径应将它放在内部RAM。
// delta has467int16_t entries and should live in internal RAM for the worker hot path.
// 支持全部身体动作及淡入淡出，pose_ms须为50ms关键帧，权重与身体资源使用同一时刻。
// All body clips and fades accept50ms keyframes; eye weights and body assets use the same pose time.
bool mimi_prepare_eyes(const mimi_pet_t *pet, uint32_t pose_ms, const uint8_t *body,
                       int16_t delta[MIMI_PREPARE_SIDE+1], mimi_prepared_eyes_t *out);
