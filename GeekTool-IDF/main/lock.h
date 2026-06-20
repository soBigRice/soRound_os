#pragma once
// 锁屏控制 + BOOT 实体键 + 锁屏省电(M3b)。
#include <stdbool.h>

void lock_init(void);          // 建表盘 + 按键轮询 + 省电定时器,须在 LVGL 任务/锁内
void lock_set(bool locked);    // 进入/退出锁屏(表盘)
bool lock_is_locked(void);
