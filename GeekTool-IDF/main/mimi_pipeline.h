#pragma once
#include "mimi_assets.h"
#include "mimi_prepare.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct mimi_pipeline mimi_pipeline_t;
typedef struct {
    mimi_asset_slot_t asset;
    mimi_prepared_eyes_t eyes;
    mimi_pet_t snapshot;
    uint32_t epoch,serial;
} mimi_pipeline_frame_t;
typedef struct {
    uint32_t worker_stack_free,presented,not_ready,stop_ok,errors;
} mimi_pipeline_stats_t;
enum { MIMI_PIPELINE_ERROR_QUEUE=1u,MIMI_PIPELINE_ERROR_PREPARE=2u,
       MIMI_PIPELINE_ERROR_PROTOCOL=4u,MIMI_PIPELINE_ERROR_STOP=8u,
       MIMI_PIPELINE_ERROR_ASSET=16u };

mimi_pipeline_t *mimi_pipeline_create(const char *path);
// 仅UI调用；同一clip/epoch的完成帧可发布，即使时钟已前进，避免慢读取使画面永久饥饿。
// UI only: publish a complete frame from this clip/epoch even after its clock advances, avoiding I/O starvation.
// 返回帧在下一次new_frame=true前保持不可变；后台只写另一块缓冲，调用者不等待worker。
// The result stays immutable until the next new_frame=true; the worker writes the other slot without blocking the UI.
const mimi_pipeline_frame_t *mimi_pipeline_update(mimi_pipeline_t *,const mimi_pet_t *,bool *new_frame);
// 隐藏或切路径使旧作业失效；同clip时钟回退也自动换代，当前显示缓冲保留到下一次成功发布。
// Hiding, changing paths or rewinding a clip invalidates old jobs while retaining the display slot until successful publication.
void mimi_pipeline_invalidate(mimi_pipeline_t *);
void mimi_pipeline_stats(const mimi_pipeline_t *,mimi_pipeline_stats_t *);
// 先解除渲染引用再退出；超时保留FILE/任务/缓冲所有权，调用者稍后可重试。
// Detach rendering before shutdown; timeout retains FILE/task/buffer ownership for a later retry.
bool mimi_pipeline_destroy(mimi_pipeline_t *,mimi_pipeline_stats_t *);
