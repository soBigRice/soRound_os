#pragma once
// 圆屏居中聚焦滚动列表(可复用组件)—— WiFi / I2C / 系统信息共用。LVGL 9。
#include "lvgl.h"

// 创建一个铺满 parent 的圆屏列表(自带曲率滚动 + 居中聚焦)
lv_obj_t *ui_list_create(lv_obj_t *parent);

// 加一行:左主文 + 右副文(right 可为 NULL)。返回 row,调用方可加点击/user_data
lv_obj_t *ui_list_row(lv_obj_t *list, const char *left, const char *right, uint32_t right_color);

// 重建行后调用:立即生效布局 + 应用曲率
void ui_list_relayout(lv_obj_t *list);

// 取某行的右副文 label(用于动态更新,如系统信息的可变值);无则返回 NULL
lv_obj_t *ui_list_row_right(lv_obj_t *row);
