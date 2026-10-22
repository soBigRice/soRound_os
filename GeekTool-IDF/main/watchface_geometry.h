#pragma once
#include "watchface_ui.h"

// 六款纯几何表盘沿用分钟更新；辅助数字保持概念稿默认关闭。
// Six geometric faces use minute updates and retain the concept's default hidden numeric aid.
void watchface_geometry_render(lv_layer_t *layer, const lv_area_t *area,
                               const watchface_data_t *data, int kind, bool preview);
