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
// 水平仪 —— QMI8658。整屏大碗 + 真实重力小球:重力加速 + 惯性/摩擦 + 很弱的回中力(浅碗)。
// 任意方向稍微一倾(~18°)球就滚到碗沿,放平自动回中;背景显示倾角参数(藏在球后)。
#include "app.h"
#include "glyph.h"
#include "imu.h"
#include <math.h>
#include <stdio.h>

#define LCX 233
#define LCY 233
#define DISH 188
#define BALL_R 20
#define MAXR (DISH - BALL_R)   // 球心最大半径 168
#define ACCEL 9.8f             // 重力加速(越大越灵敏)
#define FRIC  0.85f            // 摩擦(越小越快停)
#define CTR   0.004f           // 回中力(很弱:稍倾即可到沿,放平才回中)
#define MAXV  16.0f

static lv_obj_t *g_ball, *g_big, *g_axes;
static float ox, oy, vx, vy;

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
    lv_label_set_text(g_axes, imu_init() ? "" : "no sensor");
    lv_obj_align(g_axes, LV_ALIGN_CENTER, 0, 24);

    g_ball = lv_obj_create(parent);
    lv_obj_remove_style_all(g_ball);
    lv_obj_set_size(g_ball, BALL_R * 2, BALL_R * 2);
    lv_obj_set_pos(g_ball, LCX - BALL_R, LCY - BALL_R);
    lv_obj_set_style_radius(g_ball, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_ball, lv_color_hex(0x36e0c0), 0);
    lv_obj_set_style_bg_opa(g_ball, LV_OPA_COVER, 0);
    lv_obj_add_flag(g_ball, LV_OBJ_FLAG_EVENT_BUBBLE);

    ox = oy = vx = vy = 0;
}

static void level_tick(void) {
    if (!g_ball) return;
    float tx, ty, az;
    {   // 取倾斜分量 + z(算总倾角用)
        float a3x, a3y;
        if (!imu_read_tilt(&tx, &ty)) return;
        imu_read_accel(&a3x, &a3y, &az);
    }
    // 真实重力:加速 + 摩擦 + 很弱回中(浅碗)
    vx += tx * ACCEL - ox * CTR;
    vy += ty * ACCEL - oy * CTR;
    vx *= FRIC; vy *= FRIC;
    float sp = sqrtf(vx * vx + vy * vy);
    if (sp > MAXV) { vx = vx * MAXV / sp; vy = vy * MAXV / sp; }
    ox += vx; oy += vy;
    float d = sqrtf(ox * ox + oy * oy);
    if (d > MAXR) { ox = ox * MAXR / d; oy = oy * MAXR / d; vx *= 0.3f; vy *= 0.3f; }   // 贴碗沿
    lv_obj_set_pos(g_ball, LCX + (int)ox - BALL_R, LCY + (int)oy - BALL_R);

    float tilt = sqrtf(tx * tx + ty * ty);
    uint32_t col = tilt < 0.04f ? 0x36e0c0 : (tilt < 0.25f ? COL_TXT : COL_RED);
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
