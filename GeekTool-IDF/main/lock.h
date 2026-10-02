#pragma once
// 锁屏控制 + BOOT 短按锁/解、长按关机 + 锁屏省电。PWR 归计时 App 使用。
#include <stdbool.h>

void lock_init(void);          // 建表盘 + 按键轮询 + 省电定时器,须在 LVGL 任务/锁内
void lock_set(bool locked);    // 进入/退出锁屏(表盘)
bool lock_is_locked(void);
