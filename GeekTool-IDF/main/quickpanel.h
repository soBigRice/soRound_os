#pragma once
// 锁屏表盘下拉快捷面板:WiFi / always-on(长显)/ 静音 三个圆形开关 + 亮度滑条。
// 在表盘上【下拉】(BOTTOM 手势)打开、【上滑】(TOP 手势)或点把手关闭,整块面板
// 用 translate_y 从屏幕上方滑入/滑出(经典通知栏动画)。挂在 wf_screen 上,随锁屏显示。
#include "lvgl.h"
#include <stdbool.h>

void quickpanel_init(lv_obj_t *parent);   // 在 wf_screen 上构建一次(隐藏 + 移到屏幕上方待命)
void quickpanel_open(void);
void quickpanel_close(void);
bool quickpanel_is_open(void);
void quickpanel_hide(void);                // 立即隐藏并复位(解锁/隐藏表盘时调,免残留)
