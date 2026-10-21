#pragma once
#include "lvgl.h"

typedef struct {
    lv_obj_t *panel;
    lv_obj_t *preview;
} watchface_picker_t;

// 状态须零初始化；调用者拥有专用panel。复用原层级，不额外嵌套绘制容器。
// Zero-initialize the state; the caller owns a dedicated panel. Preserve the existing draw depth.
void watchface_picker_mount(watchface_picker_t *picker, lv_obj_t *panel);
void watchface_picker_refresh(watchface_picker_t *picker);
// 删除panel或退出App前取消待执行重建；不销毁调用者的对象。
// Cancel pending rebuilds before exit or panel deletion; the caller retains object ownership.
void watchface_picker_unmount(watchface_picker_t *picker);
