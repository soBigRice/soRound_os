#pragma once
#include "watchface_ui.h"

// 六款极简排版共用时间快照和分钟刷新策略。
// Six minimal typography faces share the clock snapshot and minute-only refresh policy.
void watchface_type_render(lv_layer_t *layer, const lv_area_t *area,
                           const watchface_data_t *data, int kind, bool preview);
