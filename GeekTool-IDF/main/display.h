#pragma once
#include "lvgl.h"
#include "driver/i2c_master.h"

// 初始化 CO5300 QSPI 面板 + lvgl_port(双缓冲/DMA/无撕裂),返回 LVGL 显示句柄
lv_display_t *display_init(void);

// 初始化 CST9217 触摸,挂到 lvgl_port
void touch_init(i2c_master_bus_handle_t i2c_bus, lv_display_t *disp);

// 屏幕亮度(CO5300 命令 0x51,0-255)与熄屏(关面板省电)
void display_set_brightness(uint8_t level);
void display_sleep(bool sleep);   // true=熄屏(面板关),false=亮屏
