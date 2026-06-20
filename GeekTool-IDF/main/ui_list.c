// 圆屏居中聚焦滚动列表 —— 从 Arduino 版移植到 LVGL 9。
#include "ui_list.h"
#include "app.h"
#include "board_config.h"
#include <math.h>

#define FADE_R  210   // 透明度衰减范围
#define CURVE_R 300   // 水平曲率半径(越大越平缓)
#define ROW_W   384
#define ROW_H   58

/* 滚动时:每行按到中心距离做 translate_x + 淡出(便宜又稳,勿用 transform) */
static void curve_scroll_cb(lv_event_t *e) {
    lv_obj_t *cont = lv_event_get_target_obj(e);
    lv_area_t a; lv_obj_get_coords(cont, &a);
    int32_t cy = a.y1 + lv_area_get_height(&a) / 2;

    uint32_t cnt = lv_obj_get_child_count(cont);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t *row = lv_obj_get_child(cont, i);
        lv_area_t r; lv_obj_get_coords(row, &r);
        int32_t ry   = r.y1 + lv_area_get_height(&r) / 2;
        int32_t diff = LV_ABS(ry - cy);

        int32_t dc = diff > CURVE_R ? CURVE_R : diff;
        int32_t x  = CURVE_R - (int32_t)sqrtf((float)(CURVE_R * CURVE_R - dc * dc));
        lv_obj_set_style_translate_x(row, x, 0);

        int32_t  df  = diff > FADE_R ? FADE_R : diff;
        lv_opa_t opa = lv_map(df, 0, FADE_R, LV_OPA_COVER, LV_OPA_TRANSP);
        lv_obj_set_style_opa(row, opa, 0);

        bool focus = diff < ROW_H * 0.6;
        lv_obj_set_style_bg_opa(row, focus ? LV_OPA_10 : LV_OPA_TRANSP, 0);
    }
}

lv_obj_t *ui_list_create(lv_obj_t *parent) {
    lv_obj_t *list = lv_obj_create(parent);
    lv_obj_set_size(list, LCD_H_RES, LCD_V_RES);
    lv_obj_center(list);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_style_pad_top(list, LCD_V_RES / 2 - ROW_H / 2, 0);
    lv_obj_set_style_pad_bottom(list, LCD_V_RES / 2 - ROW_H / 2, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(list, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(list, curve_scroll_cb, LV_EVENT_SCROLL, NULL);
    lv_obj_add_flag(list, LV_OBJ_FLAG_EVENT_BUBBLE);   // 让向右滑手势冒泡到 app 屏(返回)
    return list;
}

lv_obj_t *ui_list_row(lv_obj_t *list, const char *left, const char *right, uint32_t right_color) {
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_set_size(row, ROW_W, ROW_H);
    lv_obj_set_style_bg_color(row, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, ROW_H / 2, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);   // 手势冒泡到 app 屏

    lv_obj_t *l = lv_label_create(row);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(l, right ? ROW_W - 150 : ROW_W - 48);
    lv_label_set_text(l, left);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_set_style_text_font(l, UI_FONT_L, 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 24, 0);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);

    if (right) {
        lv_obj_t *r = lv_label_create(row);
        lv_label_set_text(r, right);
        lv_obj_set_style_text_color(r, lv_color_hex(right_color), 0);
        lv_obj_set_style_text_font(r, UI_FONT_M, 0);
        lv_obj_align(r, LV_ALIGN_RIGHT_MID, -22, 0);
        lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE);
    }
    return row;
}

lv_obj_t *ui_list_row_right(lv_obj_t *row) {
    return (lv_obj_get_child_count(row) >= 2) ? lv_obj_get_child(row, 1) : NULL;
}

void ui_list_relayout(lv_obj_t *list) {
    lv_obj_update_layout(list);
    lv_obj_send_event(list, LV_EVENT_SCROLL, NULL);
}
