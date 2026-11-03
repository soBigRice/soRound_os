#pragma once
#include "lvgl.h"
#include "mimi_assets.h"
#include "mimi_prepare.h"
#include <stddef.h>
typedef struct mimi_render mimi_render_t;
size_t mimi_render_bytes(void);
mimi_render_t *mimi_render_init(void *memory);
const uint16_t *mimi_render_pixels(const mimi_render_t *renderer);
bool mimi_render_attach(mimi_render_t *renderer,const void *image_source);
// UI线程只引用已经完成的不可变slot，发布与回收由pipeline负责。
// The UI references only completed immutable slots; the pipeline owns publication and reclamation.
bool mimi_render_select(mimi_render_t *renderer,const mimi_asset_frame_t *frame,const mimi_prepared_eyes_t *eyes);
void mimi_render_frame(mimi_render_t *renderer,uint16_t *target,int stride,lv_area_t clip,bool swapped);
