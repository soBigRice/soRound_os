#pragma once
// 锁屏表盘框架(多表盘可切换 + 低功耗 AOD)。各表盘实现 watchface_t,注册进 FACES[]。
// 全屏黑底(AMOLED 省电),Nothing 单色 + 唯一红强调。锁屏左右滑换表盘,上滑解锁(lock.c)。
#include "lvgl.h"
#include <stdbool.h>

void watchface_init(void);        // 构建一次(隐藏)+ 载入上次选择的表盘,须在 LVGL 任务/锁内调用
void watchface_show(void);
void watchface_hide(void);
bool watchface_visible(void);
lv_obj_t *watchface_root(void);   // 顶层全屏对象,供 lock 挂上滑解锁手势

// 低功耗:AOD 态停止闪烁/秒点,只按分钟刷新(变暗由 lock.c 控亮度)
void watchface_set_aod(bool aod);

// 表盘选择
int         watchface_count(void);
const char *watchface_name(int idx);
void        watchface_select(int idx);   // 钳到 [0,count) 并重建当前内容
void        watchface_next(int dir);     // 循环切换 +1/-1,存 NVS,短暂显示表盘名
