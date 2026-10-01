// 粒子重力流体 —— 连续坐标 Verlet 粒子物理 + QMI8658 重力方向(倾斜手表,液体往低处流)。
// v3(前两版是 4px 元胞自动机,格子跳变颗粒感重):240 颗半径 4-6px 圆粒,浮点位置逐帧积分,
// 位置式软碰撞(粒-粒分离 + 圆壁约束,Verlet 隐式速度天然稳定),独立渲染定时器 —— 丝滑水珠感。
// 渲染:464x464 RGB565 画布(PSRAM),预计算圆盘行宽表增量擦/画,每帧只无效化最多 8 条脏带;
// 全部静止或放平 → 零模拟零推屏(配合 light sleep)。无 IMU 时重力朝下。
#include "app.h"
#include "imu.h"
#include "motion.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_pm.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define BW      464             // 画布像素宽高
#define OFF     1               // 画布偏移,464 居中于 466
#define CTR     232.0f          // 圆心(画布本地坐标)
#define R_WALL  227.0f          // 液体活动半径(屏半径 233 留 6px 贴边)
#define NPART   240             // 粒子数
#define R_MIN   4
#define R_MAX   6
#define GRAV    0.72f           // 满倾斜加速度(px/子步²):抬高 → 起步更猛、跟手
#define DRAG    0.996f          // 速度阻尼(每子步;越接近 1 越"稀"越活):放松 → 惯性更足、更灵活
#define V_MAX   6.0f            // 每 8.25ms 子步限速 ≈ 粒径,防穿透
#define ITERS   2               // 每子步约束求解迭代
#define HCELL   13              // 空间哈希格边长(≥最大直径)
#define HW      (BW / HCELL + 1)

typedef struct {
    float   x, y, px, py;       // 当前/上帧位置(Verlet:速度 = 差分)
    int16_t ix, iy;             // 当前画在画布上的整数位置(擦除用)
    int16_t nix, niy;           // 本帧新整数位置(渲染前算好)
    uint8_t r, col, mv;         // 半径 4-6 / 颜色索引 / 本帧是否移动(增量渲染用)
} part_t;

static part_t  *g_p;            // 粒子 + 哈希表同块 malloc(退出经 LV_EVENT_DELETE 释放)
static int16_t *g_head, *g_next;
static uint16_t *g_buf;         // 画布(PSRAM ~431KB)
static lv_obj_t *g_canvas, *g_hint;
static uint16_t  g_lut[3];      // 0=黑 1=白 2=红
static int8_t    g_span[R_MAX + 1][2 * R_MAX + 1];   // 圆盘每行半宽(擦/画零 sqrt)
static bool      g_has_imu, g_asleep;
static uint32_t  g_calm;                             // 累计静止微秒
static float     g_ltx, g_lty;                       // 入睡倾斜(累计唤醒判断)
static int       g_dx1, g_dy1, g_dx2, g_dy2;         // 本帧脏矩形(像素)
#define DIRTY_BANDS 8
#define BAND_H (BW / DIRTY_BANDS)
static lv_area_t g_dirty[DIRTY_BANDS];
static uint32_t g_remainder;
static int64_t g_last_us;
static bool g_pm_held;
static lv_timer_t *g_timer;
static esp_pm_lock_handle_t g_pm;                    // 钉住 CPU 240MHz:物理+渲染吃满算力才丝滑(DFS 空闲会掉到 80)

static uint32_t rnd(void) {
    static uint32_t s = 0x9d2c5681;
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s;
}

static void pm_hold(bool held) {
    if (!g_pm || held == g_pm_held) return;
    if (held) esp_pm_lock_acquire(g_pm); else esp_pm_lock_release(g_pm);
    g_pm_held = held;
}
static inline void mark_dirty(int x1, int y1, int x2, int y2) {
    if (x1 < g_dx1) g_dx1 = x1;
    if (y1 < g_dy1) g_dy1 = y1;
    if (x2 > g_dx2) g_dx2 = x2;
    if (y2 > g_dy2) g_dy2 = y2;
    for (int b = y1 / BAND_H; b <= y2 / BAND_H && b < DIRTY_BANDS; b++) {
        lv_area_t *a = &g_dirty[b];
        int top = y1 > b * BAND_H ? y1 : b * BAND_H;
        int bottom = y2 < (b + 1) * BAND_H - 1 ? y2 : (b + 1) * BAND_H - 1;
        if (x1 < a->x1) a->x1 = x1;
        if (top < a->y1) a->y1 = top;
        if (x2 > a->x2) a->x2 = x2;
        if (bottom > a->y2) a->y2 = bottom;
    }
}

static void draw_disc(int cx, int cy, int r, uint16_t c) {
    for (int dy = -r; dy <= r; dy++) {
        int w = g_span[r][dy + r];
        uint16_t *p = g_buf + (cy + dy) * BW + cx - w;
        for (int i = 0; i <= 2 * w; i++) p[i] = c;
    }
    mark_dirty(cx - r, cy - r, cx + r, cy + r);
}

/* 一帧:按经过时间跑固定物理小步 → 增量擦/画 → 最多 8 条脏带 */
static void fluid_frame(lv_timer_t *t) {
    (void)t;
    float tx = 0, ty = 1.0f;
    if (g_has_imu && !imu_read_tilt(&tx, &ty)) return;

    int64_t now = esp_timer_get_time();
    uint32_t elapsed = (uint32_t)(now - g_last_us);
    g_last_us = now;
    if (g_asleep) {
        if (!motion_wake(tx, ty, g_ltx, g_lty)) return;
        g_asleep = false; g_calm = 0; g_remainder = 0;
        elapsed = FLUID_STEP_US;
        lv_timer_set_period(g_timer, 20);
        pm_hold(true);
    }
    unsigned steps = motion_steps(&g_remainder, elapsed);
    if (!steps) return;

    float mag = sqrtf(tx * tx + ty * ty);
    float ax = (mag < 0.04f) ? 0 : tx * GRAV;   // 放平:无平面重力,靠阻尼自然停住
    float ay = (mag < 0.04f) ? 0 : ty * GRAV;

    // 固定 8.25ms 物理小步保留重力/阻尼和限速;渲染只在最后做一次。
    float maxd2 = 0;
    for (unsigned s = 0; s < steps; s++) {
        for (int i = 0; i < NPART; i++) {                           // Verlet 积分
            part_t *p = &g_p[i];
            float vx = (p->x - p->px) * DRAG, vy = (p->y - p->py) * DRAG;
            if (vx > V_MAX) vx = V_MAX; else if (vx < -V_MAX) vx = -V_MAX;
            if (vy > V_MAX) vy = V_MAX; else if (vy < -V_MAX) vy = -V_MAX;
            p->px = p->x; p->py = p->y;
            p->x += vx + ax; p->y += vy + ay;
            float d2 = vx * vx + vy * vy;
            if (d2 > maxd2) maxd2 = d2;
        }
        for (int it = 0; it < ITERS; it++) {
            memset(g_head, 0xFF, HW * HW * sizeof(int16_t));        // 重建空间哈希
            for (int i = 0; i < NPART; i++) {
                int c = (int)(g_p[i].y / HCELL) * HW + (int)(g_p[i].x / HCELL);
                g_next[i] = g_head[c]; g_head[c] = i;
            }
            for (int i = 0; i < NPART; i++) {                       // 粒-粒位置式分离(只解 j>i,免重复)
                part_t *a = &g_p[i];
                int cx = (int)(a->x / HCELL), cy = (int)(a->y / HCELL);
                for (int ny = cy - 1; ny <= cy + 1; ny++)
                    for (int nx = cx - 1; nx <= cx + 1; nx++) {
                        if ((unsigned)nx >= HW || (unsigned)ny >= HW) continue;
                        for (int j = g_head[ny * HW + nx]; j >= 0; j = g_next[j]) {
                            if (j <= i) continue;
                            part_t *b = &g_p[j];
                            float dx = b->x - a->x, dy = b->y - a->y;
                            float md = a->r + b->r, d2 = dx * dx + dy * dy;
                            if (d2 >= md * md || d2 < 1e-4f) continue;
                            float d = sqrtf(d2), ov = 0.5f * (md - d) / d;
                            a->x -= dx * ov; a->y -= dy * ov;
                            b->x += dx * ov; b->y += dy * ov;
                        }
                    }
            }
            for (int i = 0; i < NPART; i++) {                       // 圆壁钳位(摩擦/回弹由 Verlet 隐式给出)
                part_t *p = &g_p[i];
                float dx = p->x - CTR, dy = p->y - CTR, lim = R_WALL - p->r;
                float d2 = dx * dx + dy * dy;
                if (d2 > lim * lim) {
                    float d = sqrtf(d2), k = lim / d;
                    p->x = CTR + dx * k; p->y = CTR + dy * k;
                }
            }
        }
    }

    // === 增量渲染:只擦/画本帧真正移动的粒子。躺平的水保留在缓冲里不动,
    //     脏矩形只覆盖流动带 → 静止/半沉降时推屏面积暴跌,延迟随之消失。===
    g_dx1 = BW; g_dy1 = BW; g_dx2 = -1; g_dy2 = -1;
    for (int b = 0; b < DIRTY_BANDS; b++) g_dirty[b] = (lv_area_t){BW, BW, -1, -1};
    for (int i = 0; i < NPART; i++) {
        part_t *p = &g_p[i];
        p->nix = (int16_t)(p->x + 0.5f); p->niy = (int16_t)(p->y + 0.5f);
        p->mv  = (p->nix != p->ix || p->niy != p->iy);
    }
    for (int i = 0; i < NPART; i++)                                 // 先擦所有移动粒子的旧位(重叠才不留渣)
        if (g_p[i].mv) draw_disc(g_p[i].ix, g_p[i].iy, g_p[i].r, g_lut[0]);
    for (int i = 0; i < NPART; i++)                                 // 再画到新位
        if (g_p[i].mv) {
            g_p[i].ix = g_p[i].nix; g_p[i].iy = g_p[i].niy;
            draw_disc(g_p[i].ix, g_p[i].iy, g_p[i].r, g_lut[g_p[i].col]);
        }

    if (g_dx2 >= g_dx1) {                                           // 有粒子动过才推屏
        int rx1 = g_dx1, ry1 = g_dy1, rx2 = g_dx2, ry2 = g_dy2;     // 移动粒子边界(用来查找需要修补的静止粒子)
        int ex1 = rx1 - 2 * R_MAX, ey1 = ry1 - 2 * R_MAX, ex2 = rx2 + 2 * R_MAX, ey2 = ry2 + 2 * R_MAX;
        for (int i = 0; i < NPART; i++) {                           // 修补:与流动带重叠的静止粒子被擦掉一角 → 重画补回
            part_t *p = &g_p[i];
            if (!p->mv && p->ix >= ex1 && p->ix <= ex2 && p->iy >= ey1 && p->iy <= ey2)
                draw_disc(p->ix, p->iy, p->r, g_lut[p->col]);
        }
        lv_area_t oc;
        lv_obj_get_coords(g_canvas, &oc);
        // 最多 8 个区域;不把分散移动的水滴之间大片黑底合成一个推屏矩形。
        for (int b = 0; b < DIRTY_BANDS; b++) {
            lv_area_t a = g_dirty[b];
            if (a.x1 > a.x2) continue;
            a.x1 += oc.x1; a.x2 += oc.x1;
            a.y1 += oc.y1; a.y2 += oc.y1;
            lv_obj_invalidate_area(g_canvas, &a);
        }
    }

    if (maxd2 < 0.004f) g_calm += steps * FLUID_STEP_US;
    else g_calm = 0;
    if (g_calm >= 693000) {
        g_asleep = true; g_ltx = tx; g_lty = ty;
        for (int i = 0; i < NPART; i++) { g_p[i].px = g_p[i].x; g_p[i].py = g_p[i].y; }
        lv_timer_set_period(g_timer, 33);
        pm_hold(false);
    }
}

static void buf_deleted(lv_event_t *e)  { heap_caps_free(lv_event_get_user_data(e)); }   // 删屏是 async 的,
static void mem_deleted(lv_event_t *e)  { free(lv_event_get_user_data(e)); }             // 真正删除时才释放

static void fluid_enter(lv_obj_t *parent) {
    size_t sz_p = NPART * sizeof(part_t), sz_h = HW * HW * sizeof(int16_t), sz_n = NPART * sizeof(int16_t);
    uint8_t *blk = malloc(sz_p + sz_h + sz_n);
    g_buf = heap_caps_malloc(BW * BW * 2, MALLOC_CAP_SPIRAM);     // 画布进 PSRAM,不占内部 RAM
    if (!blk || !g_buf) {
        free(blk);
        if (g_buf) { heap_caps_free(g_buf); g_buf = NULL; }
        return;
    }
    g_p = (part_t *)blk;
    g_head = (int16_t *)(blk + sz_p);
    g_next = (int16_t *)(blk + sz_p + sz_h);
    memset(g_buf, 0, BW * BW * 2);

    g_lut[0] = lv_color_to_u16(lv_color_black());
    g_lut[1] = lv_color_to_u16(lv_color_hex(COL_TXT));
    g_lut[2] = lv_color_to_u16(lv_color_hex(COL_RED));

    for (int r = R_MIN; r <= R_MAX; r++)        // 预计算圆盘行宽表(渲染零 sqrt)
        for (int dy = -r; dy <= r; dy++)
            g_span[r][dy + r] = (int8_t)sqrtf((float)(r * r - dy * dy));

    for (int i = 0; i < NPART; i++) {           // 圆内随机撒粒(初始重叠由求解器一两帧内自然弹开)
        part_t *p = &g_p[i];
        float a = (rnd() % 6283) / 1000.0f, d = sqrtf((rnd() % 1000) / 1000.0f) * (R_WALL - R_MAX - 2);
        p->x = p->px = CTR + cosf(a) * d;
        p->y = p->py = CTR + sinf(a) * d;
        p->r = R_MIN + rnd() % (R_MAX - R_MIN + 1);
        p->col = (rnd() % 100 < 10) ? 2 : 1;
        p->ix = (int16_t)p->x; p->iy = (int16_t)p->y;
        draw_disc(p->ix, p->iy, p->r, g_lut[p->col]);
    }

    g_canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(g_canvas, g_buf, BW, BW, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(g_canvas, OFF, OFF);
    lv_obj_add_flag(g_canvas, LV_OBJ_FLAG_EVENT_BUBBLE);          // 右滑返回手势照常冒泡
    lv_obj_add_event_cb(g_canvas, buf_deleted, LV_EVENT_DELETE, g_buf);
    lv_obj_add_event_cb(g_canvas, mem_deleted, LV_EVENT_DELETE, blk);

    g_has_imu = imu_init();
    g_hint = lv_label_create(parent);
    lv_obj_set_style_text_font(g_hint, UI_FONT_M, 0);
    lv_obj_set_style_text_color(g_hint, lv_color_hex(COL_TXT2), 0);
    lv_label_set_text(g_hint, g_has_imu ? "" : tr(S_FLUID_NOIMU));
    lv_obj_align(g_hint, LV_ALIGN_TOP_MID, 0, 90);

    if (esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "fluid", &g_pm) == ESP_OK)
        pm_hold(true);                               // 仅流动时锁 240MHz;入睡/遮挡释放

    g_asleep = false; g_calm = 0; g_ltx = g_lty = 0;
    g_last_us = esp_timer_get_time(); g_remainder = 0;
    g_timer = lv_timer_create(fluid_frame, 20, NULL);             // 50Hz 渲染,固定时间物理步进
}

static void fluid_exit(void) {
    if (g_timer) { lv_timer_delete(g_timer); g_timer = NULL; }    // 先停定时器,再由删屏回调释放缓冲
    if (g_pm) { pm_hold(false); esp_pm_lock_delete(g_pm); g_pm = NULL; }
    g_canvas = g_hint = NULL;
    g_p = NULL; g_head = g_next = NULL; g_buf = NULL;
}

static void fluid_visibility(bool visible) {
    if (!g_timer) return;
    if (visible) {
        g_last_us = esp_timer_get_time(); g_remainder = 0;
        pm_hold(!g_asleep); lv_timer_resume(g_timer);
    } else {
        lv_timer_pause(g_timer); pm_hold(false);
    }
}

const app_t app_fluid = { "fluid", COL_TXT, fluid_enter, NULL, fluid_exit, NULL, 0, fluid_visibility };
