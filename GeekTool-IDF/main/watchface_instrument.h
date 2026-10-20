#pragma once

#include "watchface_ui.h"

// 器械三款共用实时数据；kind 依次为雷达、磁带、光圈。 / Live instrument faces: radar, tape, iris.
void watchface_instrument_render(lv_layer_t *layer, const lv_area_t *area,
                                const watchface_data_t *data, int kind, bool preview);
