#pragma once
// 图片表盘存储:挂载 Flash 上的 FAT 'storage' 分区(只读),把 images/bg.jpg 解码成 RGB565 放 PSRAM。
// 一次性解码并缓存,供 image 表盘背景使用。无图/解码失败返回 NULL(表盘会回退提示)。
#include "lvgl.h"

const lv_image_dsc_t *img_store_face_image(void);
