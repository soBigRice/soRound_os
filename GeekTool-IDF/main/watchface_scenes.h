#pragma once

#include "watchface_ui.h"

// 六款玩物与自然表盘共用实际时间快照；kind 按设置目录顺序排列。
// The six playful/natural faces share the real time snapshot; kind follows the catalog order.
void watchface_scenes_render(lv_layer_t *layer, const lv_area_t *area,
                             const watchface_data_t *data, int kind, bool preview);
