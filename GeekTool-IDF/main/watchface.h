#pragma once
// Nothing 风格点阵表盘(锁屏)。全屏黑底,5×7 点阵时间 + 点阵外环 + 红点冒号。
#include "lvgl.h"
#include <stdbool.h>

void watchface_init(void);        // 构建一次(隐藏),须在 LVGL 任务/锁内调用
void watchface_show(void);
void watchface_hide(void);
bool watchface_visible(void);
lv_obj_t *watchface_root(void);   // 顶层全屏对象,供 lock 挂上滑解锁手势
