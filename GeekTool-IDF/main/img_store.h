#pragma once
// 图片表盘存储:挂载 Flash 上的 FAT 'storage' 分区(只读),把 images/bg.jpg 解码成 RGB565 放 PSRAM。
// 一次性解码并缓存,供 image 表盘背景使用。解码中/无图/解码失败返回 NULL(表盘会回退提示)。
#include "lvgl.h"

const lv_image_dsc_t *img_store_face_image(void);

bool img_store_loading(void); // 首次读取在后台解码;就绪后 face_image 返回缓存

// 自定义 bg.jpg 优先，缺失/不可解码时使用固件内置主题背景；每主题最多缓存一份 RGB565。
const lv_image_dsc_t *img_store_face_image_for(int theme);
bool img_store_face_loading(int theme);
