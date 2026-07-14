#pragma once
// BOOT 实体键(GPIO0,按下=0)轮询 —— 秒表/倒计时共用的开始/暂停键。
// 用法:app enter 里调 bootkey_init()(幂等,并把"按住进入 app"视为松开);
// 50ms tick 里调 bootkey_pressed(),按下沿返回一次 true(轮询周期天然去抖 + 200ms 间隔保险)。
#include <stdbool.h>

void bootkey_init(void);
bool bootkey_pressed(void);
