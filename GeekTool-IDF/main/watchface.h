#pragma once
// 锁屏表盘框架(多表盘 + 低功耗 AOD)。各表盘实现 watchface_t,注册进 FACES[]。
// 全屏黑底(AMOLED 省电),Nothing 单色 + 唯一红强调。表盘在设置 app 里选(不再左右滑切换)。
#include "lvgl.h"
#include <stdbool.h>

void watchface_init(void);        // 构建一次(隐藏)+ 载入上次选择的表盘,须在 LVGL 任务/锁内调用
void watchface_show(void);
void watchface_hide(void);
bool watchface_visible(void);
lv_obj_t *watchface_root(void);   // 顶层全屏对象,供 lock 挂上滑解锁手势

// 低功耗:AOD 态停止闪烁/秒点,只按分钟刷新(变暗由 lock.c 控亮度)
void watchface_set_aod(bool aod);

// 熄屏省电:面板已黑,停掉表盘刷新定时器(不再重画/推屏,CPU 得以长时间空闲进浅睡);
// 表盘对象保持可见,继续挡住底下 launcher 的触摸。唤醒时恢复并立即补画一帧。
void watchface_set_sleep(bool sleep);

// 表盘选择(设置 app 调用;selected 返回当前索引)
int         watchface_count(void);
const char *watchface_name(int idx);
int         watchface_selected(void);
void        watchface_select(int idx);   // 钳到 [0,count) 并重建当前内容(隐藏时也可调,下次显示即生效)
