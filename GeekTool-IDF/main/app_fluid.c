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
#include "fluid_ink.h"
#include "settings.h"
#include "lvgl_compat.h"
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
static unsigned g_mode,g_palette,g_ink_color;
static fluid_ink_t *g_ink;
static lv_obj_t *g_scene,*g_parent,*g_modes[2],*g_ink_controls[3];
static bool g_ink_playing=true,g_visible=true,g_touching;
static lv_point_t g_touch_point;
static int64_t g_touch_us;
static float g_ink_tx,g_ink_ty;
static void ink_frame(void);

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
    if(g_mode==1){ink_frame();return;}
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

static void particle_enter(lv_obj_t *parent) {
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

static void stop_simulation(void) {
    if (g_timer) { lv_timer_delete(g_timer); g_timer = NULL; }    // 先停定时器,再由删屏回调释放缓冲
    if (g_pm) { pm_hold(false); esp_pm_lock_delete(g_pm); g_pm = NULL; }
    g_canvas = g_hint = NULL;
    g_p = NULL; g_head = g_next = NULL; g_buf = NULL;
    g_ink=NULL;g_touching=false;
}

static void fluid_visibility(bool visible) {
    g_visible=visible;g_touching=false;
    if (!g_timer) return;
    if (visible) {
        g_last_us = esp_timer_get_time(); g_remainder = 0;
        pm_hold(!g_asleep&&(g_mode==0||g_ink_playing));
        if(g_mode==0||g_ink_playing)lv_timer_resume(g_timer);
    } else {
        lv_timer_pause(g_timer); pm_hold(false);
    }
}

static const char *fluid_word(const char *en,const char *zh){return settings_lang()?zh:en;}
static void ink_redraw(void){
    fluid_ink_dirty_t dirty[FLUID_INK_BANDS];
    if(!g_ink||!g_buf||!fluid_ink_render(g_ink,g_buf,dirty))return;
    lv_area_t bounds;lv_obj_get_coords(g_canvas,&bounds);
    for(int i=0;i<FLUID_INK_BANDS;++i)if(dirty[i].x1<=dirty[i].x2){
        lv_area_t a={bounds.x1+dirty[i].x1,bounds.y1+dirty[i].y1,bounds.x1+dirty[i].x2,bounds.y1+dirty[i].y2};
        lv_obj_invalidate_area(g_canvas,&a);
    }
}
static void ink_wake(void){
    g_asleep=false;g_calm=0;
    if(g_timer&&g_ink_playing&&g_visible){lv_timer_set_period(g_timer,20);pm_hold(true);}
}
static void ink_frame(void){
    if(!g_ink||!g_ink_playing||!g_visible)return;
    int64_t now=esp_timer_get_time();float dt=fminf((now-g_last_us)/1000000.0f,.025f);g_last_us=now;
    float tx=0,ty=0;
    if(g_has_imu&&!imu_read_tilt(&tx,&ty)){tx=g_ink_tx;ty=g_ink_ty;}
    if(g_asleep){if(!motion_wake(tx,ty,g_ltx,g_lty))return;ink_wake();dt=.02f;}
    float dx=tx-g_ink_tx,dy=ty-g_ink_ty;
    // A change of tilt stirs the closed dye volume; a constant pose must settle and sleep.
    if(fabsf(dx)+fabsf(dy)>.003f){
        fluid_ink_inject(g_ink,.5f,.5f,dx*600,dy*600,0,0);
        fluid_ink_inject(g_ink,.7f,.6f,-dy*300,dx*300,0,0);
        g_ink_tx=tx;g_ink_ty=ty;g_calm=0;
    }
    float speed=fluid_ink_step(g_ink,dt);ink_redraw();
    if(speed<.2f&&!g_touching)g_calm+=(uint32_t)(dt*1000000);else g_calm=0;
    if(g_calm>=693000){g_asleep=true;g_ltx=tx;g_lty=ty;lv_timer_set_period(g_timer,33);pm_hold(false);}
}
static void ink_touch(lv_event_t *e){
    if(!g_ink||!g_visible)return;
    lv_event_code_t code=lv_event_get_code(e);
    if(code==LV_EVENT_RELEASED||code==LV_EVENT_PRESS_LOST){if(g_touching)++g_ink_color;g_touching=false;return;}
    lv_indev_t *input=lv_indev_active();if(!input)return;
    lv_point_t point;lv_indev_get_point(input,&point);lv_area_t bounds;lv_obj_get_coords(g_canvas,&bounds);
    int64_t now=esp_timer_get_time();
    if(!g_touching){g_touch_point=point;g_touch_us=now;g_touching=true;}
    float elapsed=fmaxf((now-g_touch_us)/1000000.0f,.008f);
    float dx=(point.x-g_touch_point.x)/(float)FLUID_INK_WIDTH*FLUID_INK_GRID/elapsed;
    float dy=(point.y-g_touch_point.y)/(float)FLUID_INK_WIDTH*FLUID_INK_GRID/elapsed;
    int steps=LV_MAX(1,(int)ceilf(hypotf(point.x-g_touch_point.x,point.y-g_touch_point.y)/7));
    for(int i=1;i<=steps;++i){float t=i/(float)steps;
        float px=(g_touch_point.x+(point.x-g_touch_point.x)*t-bounds.x1)/(FLUID_INK_WIDTH-1);
        float py=(g_touch_point.y+(point.y-g_touch_point.y)*t-bounds.y1)/(FLUID_INK_WIDTH-1);
        fluid_ink_inject(g_ink,px,py,dx/steps,dy/steps,g_ink_color,.4f);
    }
    g_touch_point=point;g_touch_us=now;ink_wake();ink_redraw();
}
static void ink_enter(lv_obj_t *parent){
    g_ink=heap_caps_malloc(fluid_ink_bytes(),MALLOC_CAP_SPIRAM);
    g_buf=heap_caps_malloc(FLUID_INK_WIDTH*FLUID_INK_WIDTH*2,MALLOC_CAP_SPIRAM);
    if(!g_ink||!g_buf){if(g_ink)heap_caps_free(g_ink);if(g_buf)heap_caps_free(g_buf);g_ink=NULL;g_buf=NULL;return;}
    memset(g_buf,0,FLUID_INK_WIDTH*FLUID_INK_WIDTH*2);fluid_ink_reset(g_ink,g_palette);g_ink_color=0;
    g_canvas=lv_canvas_create(parent);lv_canvas_set_buffer(g_canvas,g_buf,FLUID_INK_WIDTH,FLUID_INK_WIDTH,LV_COLOR_FORMAT_RGB565);
    // In dye mode a drag paints. Keep the shared header/BOOT return; don't interpret a brush stroke as back.
    ui_obj_set_gesture_bubble(g_canvas,false);ui_obj_set_event_bubble(g_canvas,false);ui_obj_set_clickable(g_canvas,true);
    lv_obj_add_event_cb(g_canvas,ink_touch,LV_EVENT_PRESSED,NULL);lv_obj_add_event_cb(g_canvas,ink_touch,LV_EVENT_PRESSING,NULL);
    lv_obj_add_event_cb(g_canvas,ink_touch,LV_EVENT_RELEASED,NULL);lv_obj_add_event_cb(g_canvas,ink_touch,LV_EVENT_PRESS_LOST,NULL);
    lv_obj_add_event_cb(g_canvas,buf_deleted,LV_EVENT_DELETE,g_buf);lv_obj_add_event_cb(g_canvas,buf_deleted,LV_EVENT_DELETE,g_ink);
    g_has_imu=imu_init();g_ink_tx=g_ink_ty=0;if(g_has_imu)imu_read_tilt(&g_ink_tx,&g_ink_ty);
    g_asleep=false;g_calm=0;g_last_us=esp_timer_get_time();g_ink_playing=true;
    if(esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX,0,"ink",&g_pm)==ESP_OK)pm_hold(true);
    g_timer=lv_timer_create(fluid_frame,20,NULL);ink_redraw();
}
static void ink_action(lv_event_t *e);
static void mode_action(lv_event_t *e);
static lv_obj_t *fluid_button(lv_obj_t *parent,int x,int y,int w,const char *name,lv_event_cb_t cb,uintptr_t action){
    lv_obj_t *button=lv_button_create(parent);lv_obj_remove_style_all(button);lv_obj_set_pos(button,x,y);lv_obj_set_size(button,w,36);
    lv_obj_set_style_radius(button,12,0);lv_obj_set_style_bg_color(button,lv_color_hex(0x16161a),0);lv_obj_set_style_bg_opa(button,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(button,1,0);lv_obj_set_style_border_color(button,lv_color_hex(0x353539),0);
    ui_obj_set_scrollable(button,false);ui_obj_set_gesture_bubble(button,false);lv_obj_add_event_cb(button,cb,LV_EVENT_CLICKED,(void *)action);
    lv_obj_t *label=lv_label_create(button);lv_obj_set_style_text_font(label,UI_FONT_M,0);lv_obj_set_style_text_color(label,lv_color_hex(COL_TXT),0);
    lv_label_set_text(label,name);lv_obj_center(label);return button;
}
static void update_controls(void){
    for(int i=0;i<2;++i)lv_obj_set_style_border_color(g_modes[i],lv_color_hex(g_mode==(unsigned)i?COL_RED:0x353539),0);
    for(int i=0;i<3;++i)ui_obj_set_hidden(g_ink_controls[i],g_mode==0);
    const char *const en[]={"Blue / teal","Green / gold","Rose / coral"};
    const char *const zh[]={"青蓝 / 陶橙","松绿 / 米金","烟紫 / 珊瑚"};
    lv_label_set_text(lv_obj_get_child(g_ink_controls[0],0),settings_lang()?zh[g_palette]:en[g_palette]);
    lv_label_set_text(lv_obj_get_child(g_ink_controls[1],0),g_ink_playing?fluid_word("Pause","暂停"):fluid_word("Play","播放"));
}
static void open_mode(void){
    stop_simulation();if(g_scene)lv_obj_delete(g_scene);
    g_scene=lv_obj_create(g_parent);lv_obj_remove_style_all(g_scene);lv_obj_set_size(g_scene,466,466);
    ui_obj_set_scrollable(g_scene,false);ui_obj_set_event_bubble(g_scene,true);
    if(g_mode==0)particle_enter(g_scene);else ink_enter(g_scene);
    if(g_hint)lv_obj_align(g_hint,LV_ALIGN_TOP_MID,0,146);
    if(!g_timer){pm_hold(false);g_hint=lv_label_create(g_scene);lv_obj_set_style_text_font(g_hint,UI_FONT_M,0);lv_label_set_text(g_hint,fluid_word("Not enough memory","内存不足"));lv_obj_align(g_hint,LV_ALIGN_TOP_MID,0,146);}
    for(int i=0;i<2;++i)if(g_modes[i])lv_obj_move_foreground(g_modes[i]);
    for(int i=0;i<3;++i)if(g_ink_controls[i])lv_obj_move_foreground(g_ink_controls[i]);
    if(!g_visible)fluid_visibility(false);
}
static void mode_action(lv_event_t *e){
    unsigned mode=(uintptr_t)lv_event_get_user_data(e);if(mode==g_mode)return;g_mode=mode;open_mode();update_controls();
}
static void ink_action(lv_event_t *e){
    if(!g_ink)return;
    unsigned action=(uintptr_t)lv_event_get_user_data(e);
    if(action==1){g_ink_playing=!g_ink_playing;
        if(g_ink_playing){g_last_us=esp_timer_get_time();ink_wake();lv_timer_resume(g_timer);}else{lv_timer_pause(g_timer);pm_hold(false);g_touching=false;}}
    else {if(action==0)g_palette=(g_palette+1)%FLUID_INK_PALETTES;fluid_ink_reset(g_ink,g_palette);g_ink_color=0;ink_wake();ink_redraw();}
    update_controls();
}
static void fluid_enter(lv_obj_t *parent){
    g_parent=parent;g_visible=true;open_mode();
    g_modes[0]=fluid_button(parent,126,89,104,fluid_word("Particles","粒子"),mode_action,0);
    g_modes[1]=fluid_button(parent,238,89,104,fluid_word("Ink","染料"),mode_action,1);
    g_ink_controls[0]=fluid_button(parent,112,383,140,"",ink_action,0);
    g_ink_controls[1]=fluid_button(parent,260,383,64,"",ink_action,1);
    g_ink_controls[2]=fluid_button(parent,332,383,40,LV_SYMBOL_REFRESH,ink_action,2);
    lv_obj_set_style_text_font(lv_obj_get_child(g_ink_controls[2],0),UI_FONT_SYM,0);
    update_controls();
}
static void fluid_exit(void){
    stop_simulation();g_scene=g_parent=NULL;
    for(int i=0;i<2;++i)g_modes[i]=NULL;
    for(int i=0;i<3;++i)g_ink_controls[i]=NULL;
}

const app_t app_fluid = { "fluid", COL_TXT, fluid_enter, NULL, fluid_exit, NULL, 0, fluid_visibility };
