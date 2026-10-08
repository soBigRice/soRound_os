#pragma once
// TYPE / ORBIT / SHIFT 各五款 + HAND 六款；旧 NVS 索引0–14保持原对应关系。
// 黑底 AMOLED + 红色强调。设置页选择；BOOT 解锁和空闲策略由 lock.c 管理。
#include "lvgl.h"
#include <stdbool.h>

void watchface_init(void);        // 构建一次(隐藏)+ 载入上次选择的表盘,须在 LVGL 任务/锁内调用
void watchface_show(void);
void watchface_hide(void);
bool watchface_visible(void);
lv_obj_t *watchface_root(void);   // 顶层全屏对象；不响应上滑解锁，解锁由 BOOT 侧键控制

// 低功耗:AOD 态停止闪烁/秒点,只按分钟刷新(变暗由 lock.c 控亮度)
void watchface_set_aod(bool aod);

// 熄屏省电:面板已黑,停掉表盘刷新定时器(不再重画/推屏,CPU 得以长时间空闲进浅睡);
// 表盘对象保持可见,继续挡住底下 launcher 的触摸。唤醒时恢复并立即补画一帧。
void watchface_set_sleep(bool sleep);

// 表盘选择(设置 app 调用;selected 返回当前索引)
int         watchface_count(void);
const char *watchface_name(int idx);
const char *watchface_theme_name(int theme);
const char *watchface_kind_name(int idx);
int         watchface_theme_for(int idx);
int         watchface_index_in_theme(int theme, int current);
int         watchface_selected(void);
void        watchface_select(int idx);   // 钳到 [0,count)，刷新当前内容；隐藏时下次显示生效
// 233px 原生缩略图，共用实际渲染。预览无独立计时器，设置页负责刷新/销毁。
lv_obj_t *watchface_create_preview(lv_obj_t *parent, int idx);
void watchface_refresh_preview(lv_obj_t *preview);
