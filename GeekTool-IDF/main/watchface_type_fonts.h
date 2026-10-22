// 极简大字独立字库及半尺寸预览。 / Separate oversized fonts and paired previews.
#pragma once
#include "lvgl.h"
typedef enum {WFT_REGULAR, WFT_MEDIUM, WFT_BLACK} wft_weight_t;
LV_FONT_DECLARE(font_type_regular_81);
LV_FONT_DECLARE(font_type_regular_83);
LV_FONT_DECLARE(font_type_regular_88);
LV_FONT_DECLARE(font_type_regular_161);
LV_FONT_DECLARE(font_type_regular_166);
LV_FONT_DECLARE(font_type_regular_176);
LV_FONT_DECLARE(font_type_semibold_83);
LV_FONT_DECLARE(font_type_semibold_108);
LV_FONT_DECLARE(font_type_semibold_166);
LV_FONT_DECLARE(font_type_semibold_215);
LV_FONT_DECLARE(font_type_black_100);
LV_FONT_DECLARE(font_type_black_101);
LV_FONT_DECLARE(font_type_black_110);
LV_FONT_DECLARE(font_type_black_200);
LV_FONT_DECLARE(font_type_black_202);
LV_FONT_DECLARE(font_type_black_220);
static inline const lv_font_t *wft_font(int size,bool preview,wft_weight_t weight){
int pixels=preview?(size+1)/2:size;
switch(weight){
case WFT_REGULAR:switch(pixels){
case 81:return &font_type_regular_81;
case 83:return &font_type_regular_83;
case 88:return &font_type_regular_88;
case 161:return &font_type_regular_161;
case 166:return &font_type_regular_166;
case 176:return &font_type_regular_176;
}break;
case WFT_MEDIUM:switch(pixels){
case 83:return &font_type_semibold_83;
case 108:return &font_type_semibold_108;
case 166:return &font_type_semibold_166;
case 215:return &font_type_semibold_215;
}break;
case WFT_BLACK:switch(pixels){
case 100:return &font_type_black_100;
case 101:return &font_type_black_101;
case 110:return &font_type_black_110;
case 200:return &font_type_black_200;
case 202:return &font_type_black_202;
case 220:return &font_type_black_220;
}break;
}return NULL;
}
