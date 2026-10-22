#pragma once
#include "watchface_ui.h"

// 追加的六款图形表盘只读取同一时钟快照，不创建资源或计时器。
// Six appended graphic faces read the shared clock snapshot without owning resources or timers.
void watchface_graphic_render(lv_layer_t *layer, const lv_area_t *area,
                             const watchface_data_t *data, int kind, bool preview);
