#pragma once
#include "watchface_ui.h"

// 八款设计扩展共用时钟快照、绘制原语和常显策略，无独立资源生命周期。
// Eight design extensions share the clock snapshot, drawing primitives and AOD policy without owning resources.
void watchface_atelier_render(lv_layer_t *layer, const lv_area_t *area,
                             const watchface_data_t *data, int kind, bool preview);
