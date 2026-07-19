/*
 * @Author: superRice
 * @Date: 2026-06-22 12:20:25
 * @LastEditors: superRice 1246333567@qq.com
 * @LastEditTime: 2026-06-22 13:11:40
 * @FilePath: /wxesp32/GeekTool-IDF/main/app_level.c
 * @Description: 
 * Do your best to be yourself
 * Copyright (c) 2026 by superRice, All Rights Reserved. 
 */
// 水平仪 —— QMI8658 加速度计。标准水平仪算法:倾角【直接】映射气泡位置(无重力加速/惯性/摩擦),只做轻度消抖。
// 放平=居中 0°,倾斜则气泡按倾角比例偏移,到碗沿即夹住;中心显示总倾角 + 两轴角。重力物理只迷宫 app 用。
#include "app.h"
#include "glyph.h"
#include "imu.h"
#include <math.h>
#include <stdio.h>

#define LCX 233
#define LCY 233
#define DISH 188
#define BALL_R 20
#define MAXR (DISH - BALL_R)   // 气泡最大半径 168
#define GAIN   500.0f          // 倾斜分量 → 像素:约 ±20° 时气泡到碗沿
#define SMOOTH 0.35f           // 轻度低通消抖(只为去抖,不是惯性/动量 —— 松手立即停在当前倾角)

static lv_obj_t *g_ball, *g_big, *g_axes;
static float ox, oy;

static void level_enter(lv_obj_t *parent) {
    glyph_circle(parent, LCX, LCY, DISH, 16, 2, COL_TXT2);   // 碗沿
    glyph_circle(parent, LCX, LCY, 40,   12, 2, COL_TXT2);   // 中心靶
    glyph_line(parent, LCX - 22, LCY, LCX + 22, LCY, 11, 2, COL_TXT2);
    glyph_line(parent, LCX, LCY - 22, LCX, LCY + 22, 11, 2, COL_TXT2);

    // 背景参数(暗灰,藏在球后:放平时被球盖住,倾斜时球移开即显示当前倾角)
    g_big = lv_label_create(parent);
    lv_obj_set_style_text_font(g_big, UI_FONT_SYM, 0);
    lv_obj_set_style_text_color(g_big, lv_color_hex(COL_TXT2), 0);
    lv_obj_set_style_text_align(g_big, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(g_big, "");
    lv_obj_align(g_big, LV_ALIGN_CENTER, 0, -10);

    g_axes = lv_label_create(parent);
    lv_obj_set_style_text_font(g_axes, UI_FONT_M, 0);
    lv_obj_set_style_text_color(g_axes, lv_color_hex(0x55555a), 0);
    lv_obj_set_style_text_align(g_axes, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(g_axes, imu_init() ? "" : tr(S_NO_SENSOR));
    lv_obj_align(g_axes, LV_ALIGN_CENTER, 0, 24);

    g_ball = lv_obj_create(parent);
    lv_obj_remove_style_all(g_ball);
    lv_obj_set_size(g_ball, BALL_R * 2, BALL_R * 2);
    lv_obj_set_pos(g_ball, LCX - BALL_R, LCY - BALL_R);
    lv_obj_set_style_radius(g_ball, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_ball, lv_color_hex(0x36e0c0), 0);
    lv_obj_set_style_bg_opa(g_ball, LV_OPA_COVER, 0);
    lv_obj_add_flag(g_ball, LV_OBJ_FLAG_EVENT_BUBBLE);

    ox = oy = 0;
}

static void level_tick(void) {
    if (!g_ball) return;
    float tx, ty, az;
    {   // 取倾斜分量 + z(算总倾角用)
        float a3x, a3y;
        if (!imu_read_tilt(&tx, &ty)) return;
        imu_read_accel(&a3x, &a3y, &az);
    }
    // 标准水平仪:倾角【直接】映射气泡位置(无加速/惯性/摩擦),只做轻度消抖
    float gx = tx * GAIN, gy = ty * GAIN;
    float d = sqrtf(gx * gx + gy * gy);
    if (d > MAXR) { gx = gx * MAXR / d; gy = gy * MAXR / d; }   // 夹到碗沿
    ox += (gx - ox) * SMOOTH;
    oy += (gy - oy) * SMOOTH;
    lv_obj_set_pos(g_ball, LCX + (int)ox - BALL_R, LCY + (int)oy - BALL_R);

    float tilt = sqrtf(tx * tx + ty * ty);
    uint32_t col = tilt < 0.018f ? 0x36e0c0 : (tilt < 0.12f ? COL_TXT : COL_RED);   // <~1° 绿(水平)/ <~7° 白 / 更大红
    lv_obj_set_style_bg_color(g_ball, lv_color_hex(col), 0);

    // 参数:总倾角 + 两轴角(度)
    float azc = fabsf(az) < 0.05f ? 0.05f : fabsf(az);
    int total = (int)(asinf(tilt > 1.0f ? 1.0f : tilt) * 57.2958f + 0.5f);
    char b[16]; snprintf(b, sizeof b, "%d", total);
    lv_label_set_text(g_big, b);
    char a[24]; snprintf(a, sizeof a, "x %+d  y %+d",
                         (int)(atan2f(tx, azc) * 57.2958f), (int)(atan2f(ty, azc) * 57.2958f));
    lv_label_set_text(g_axes, a);
}

static void level_exit(void) { g_ball = g_big = g_axes = NULL; }

const app_t app_level = { "level", COL_TXT, level_enter, level_tick, level_exit };
