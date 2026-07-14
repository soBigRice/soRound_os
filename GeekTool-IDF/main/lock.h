#pragma once
// 锁屏控制 + PWRON 侧键 + 锁屏省电(M3b)。BOOT 键归秒表/倒计时用(bootkey.c)。
#include <stdbool.h>

void lock_init(void);          // 建表盘 + 按键轮询 + 省电定时器,须在 LVGL 任务/锁内
void lock_set(bool locked);    // 进入/退出锁屏(表盘)
bool lock_is_locked(void);
