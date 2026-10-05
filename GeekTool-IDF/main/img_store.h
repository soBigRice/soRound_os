#pragma once
// 图片表盘存储:只读挂载 FAT 'storage'，把真正自定义的 bg.jpg 解码为 PSRAM RGB565。
// 一次性解码并缓存,供 image 表盘背景使用。解码中/无图/解码失败返回 NULL(表盘会回退提示)。
#include "lvgl.h"

const lv_image_dsc_t *img_store_face_image(void);

bool img_store_loading(void); // 首次读取在后台解码;就绪后 face_image 返回缓存

// 自定义 bg.jpg 优先；旧出厂熊猫、缺失/不可解码时使用固件主题背景。每主题最多一份缓存。
const lv_image_dsc_t *img_store_face_image_for(int theme);
bool img_store_face_loading(int theme);
