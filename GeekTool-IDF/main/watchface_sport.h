#pragma once
#include "watchface_ui.h"

// 六款运动图形只编码时分秒，不访问健康数据或增加计时器。
// Six sport-inspired graphics encode clock time without health data or additional timers.
void watchface_sport_render(lv_layer_t *layer, const lv_area_t *area,
                            const watchface_data_t *data, int kind, bool preview);
