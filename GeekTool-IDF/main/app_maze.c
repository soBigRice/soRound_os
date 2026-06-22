// 重力感应迷宫小球 —— 每次进入 / 过关都【随机生成】一座迷宫(递归回溯法,保证连通可解)。
// 6×6 网格,墙=矩形;小球用加速度计倾斜驱动(平放=中立,往哪偏往哪滚),滚进右下角红色目标即过关。
#include "app.h"
#include "glyph.h"
#include "imu.h"
#include "esp_random.h"
#include <math.h>

#define MCX 233
#define MCY 233
#define N      6            // 网格 N×N
#define CELL   48           // 每格像素
#define WT     6            // 墙厚
#define OX     89           // 网格左上(89..89+288=377,居中落在圆内)
#define OY     89
#define BALL_R 7
#define BOUND_R 215         // 圆屏兜底
// 真实物理:a = 倾斜分量 × g × 像素/米;按时间(秒)积分,分子步防穿墙
#define G_MS2  9.8f         // 真实重力
#define PPM    150.0f       // 像素/米(屏幕很小,取手感值;越大越快)
#define DT_S   0.05f        // tick 周期(s)
#define SUBS   5            // 物理子步
#define BDAMP  0.997f       // 每子步阻尼(接近无摩擦的滚动)
#define VMAX   560.0f       // px/s 上限(= BALL_R / 子步dt,防穿墙)
#define MAXW   100

typedef struct { int x, y, w, h; } rect_t;
static uint8_t  cellw[N][N];        // bit0 上 / bit1 右 / bit2 下 / bit3 左 墙;bit4 已访问
static rect_t   s_walls[MAXW];
static int      s_nwall;
static lv_obj_t *g_wallbox, *g_ball, *g_msg;
static float    bx, by, vx, vy;
static int      s_win;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* 递归回溯生成完美迷宫 */
static void maze_gen(void) {
    for (int r = 0; r < N; r++) for (int c = 0; c < N; c++) cellw[r][c] = 0x0F;   // 四面墙,未访问
    int st[N * N][2], sp = 0;
    cellw[0][0] |= 0x10; st[sp][0] = 0; st[sp][1] = 0; sp++;
    while (sp > 0) {
        int r = st[sp - 1][0], c = st[sp - 1][1];
        int dir[4], nd = 0;
        if (r > 0     && !(cellw[r - 1][c] & 0x10)) dir[nd++] = 0;   // 上
        if (c < N - 1 && !(cellw[r][c + 1] & 0x10)) dir[nd++] = 1;   // 右
        if (r < N - 1 && !(cellw[r + 1][c] & 0x10)) dir[nd++] = 2;   // 下
        if (c > 0     && !(cellw[r][c - 1] & 0x10)) dir[nd++] = 3;   // 左
        if (nd == 0) { sp--; continue; }
        int d = dir[esp_random() % nd], nr = r, nc = c;
        if (d == 0)      { cellw[r][c] &= ~0x01; nr = r - 1; cellw[nr][c] &= ~0x04; }
        else if (d == 1) { cellw[r][c] &= ~0x02; nc = c + 1; cellw[r][nc] &= ~0x08; }
        else if (d == 2) { cellw[r][c] &= ~0x04; nr = r + 1; cellw[nr][c] &= ~0x01; }
        else             { cellw[r][c] &= ~0x08; nc = c - 1; cellw[r][nc] &= ~0x02; }
        cellw[nr][nc] |= 0x10;
        st[sp][0] = nr; st[sp][1] = nc; sp++;
    }
}

static void add_wall(int x, int y, int w, int h) {
    if (s_nwall < MAXW) { s_walls[s_nwall++] = (rect_t){ x, y, w, h }; }
    lv_obj_t *o = lv_obj_create(g_wallbox);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_radius(o, 2, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(0x3a3a40), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_add_flag(o, LV_OBJ_FLAG_EVENT_BUBBLE);
}

/* 把网格墙转成矩形 + 画出来(每面墙只画一次:各格的上墙、左墙 + 下/右外边界) */
static void maze_build(void) {
    lv_obj_clean(g_wallbox);
    s_nwall = 0;
    for (int r = 0; r < N; r++) for (int c = 0; c < N; c++) {
        if (cellw[r][c] & 0x01) add_wall(OX + c * CELL - WT / 2, OY + r * CELL - WT / 2, CELL + WT, WT);   // 上
        if (cellw[r][c] & 0x08) add_wall(OX + c * CELL - WT / 2, OY + r * CELL - WT / 2, WT, CELL + WT);   // 左
    }
    for (int c = 0; c < N; c++) add_wall(OX + c * CELL - WT / 2, OY + N * CELL - WT / 2, CELL + WT, WT);   // 下边界
    for (int r = 0; r < N; r++) add_wall(OX + N * CELL - WT / 2, OY + r * CELL - WT / 2, WT, CELL + WT);   // 右边界

    int gx = OX + (N - 1) * CELL + CELL / 2, gy = OY + (N - 1) * CELL + CELL / 2;   // 目标=右下格
    glyph_circle(g_wallbox, gx, gy, 16, 11, 3, COL_RED);
    glyph_dot(g_wallbox, gx, gy, 4, COL_RED);
}

static void new_maze(void) {
    maze_gen();
    maze_build();
    bx = OX + CELL / 2; by = OY + CELL / 2;   // 起点=左上格
    vx = vy = 0;
    if (g_ball) { lv_obj_set_pos(g_ball, (int)bx - BALL_R, (int)by - BALL_R); lv_obj_set_style_bg_opa(g_ball, LV_OPA_COVER, 0); }
}

static void maze_enter(lv_obj_t *parent) {
    g_wallbox = lv_obj_create(parent);
    lv_obj_remove_style_all(g_wallbox);
    lv_obj_set_size(g_wallbox, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(g_wallbox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_wallbox, LV_OBJ_FLAG_EVENT_BUBBLE);

    g_ball = lv_obj_create(parent);
    lv_obj_remove_style_all(g_ball);
    lv_obj_set_size(g_ball, BALL_R * 2, BALL_R * 2);
    lv_obj_set_style_radius(g_ball, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_ball, lv_color_hex(COL_TXT), 0);
    lv_obj_set_style_bg_opa(g_ball, LV_OPA_COVER, 0);
    lv_obj_add_flag(g_ball, LV_OBJ_FLAG_EVENT_BUBBLE);

    g_msg = lv_label_create(parent);
    lv_obj_set_style_text_font(g_msg, UI_FONT_L, 0);
    lv_obj_set_style_text_color(g_msg, lv_color_hex(COL_RED), 0);
    lv_obj_set_style_text_align(g_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(g_msg, imu_init() ? "" : "no sensor");
    lv_obj_align(g_msg, LV_ALIGN_TOP_MID, 0, 40);

    s_win = 0;
    new_maze();
}

static void maze_tick(void) {
    if (!g_ball) return;
    if (s_win > 0) {                              // 过关闪烁,结束后换新迷宫
        s_win--;
        lv_obj_set_style_bg_opa(g_ball, ((s_win / 3) & 1) ? LV_OPA_COVER : LV_OPA_40, 0);
        if (s_win == 0) { lv_label_set_text(g_msg, ""); new_maze(); }
        return;
    }
    float tx, ty;
    if (!imu_read_tilt(&tx, &ty)) return;
    float ax = tx * G_MS2 * PPM, ay = ty * G_MS2 * PPM;   // 真实重力加速度(px/s^2)
    float sdt = DT_S / SUBS;
    for (int s = 0; s < SUBS; s++) {                       // 按时间分子步积分(球可跑很快也不穿墙)
        vx += ax * sdt; vy += ay * sdt;                    // vx/vy 单位:px/s
        vx *= BDAMP; vy *= BDAMP;
        float sp = sqrtf(vx * vx + vy * vy);
        if (sp > VMAX) { vx = vx * VMAX / sp; vy = vy * VMAX / sp; }
        float nx = bx + vx * sdt, ny = by + vy * sdt;
        for (int i = 0; i < s_nwall; i++) {               // 圆 vs 矩形
            float cx = clampf(nx, s_walls[i].x, s_walls[i].x + s_walls[i].w);
            float cy = clampf(ny, s_walls[i].y, s_walls[i].y + s_walls[i].h);
            float dx = nx - cx, dy = ny - cy, d2 = dx * dx + dy * dy;
            if (d2 < BALL_R * BALL_R) {
                float d = sqrtf(d2);
                if (d > 0.01f) {
                    float nxn = dx / d, nyn = dy / d;
                    nx = cx + nxn * BALL_R; ny = cy + nyn * BALL_R;
                    float vn = vx * nxn + vy * nyn;
                    if (vn < 0) { vx -= vn * nxn; vy -= vn * nyn; }
                } else { ny = s_walls[i].y - BALL_R; vy = 0; }
            }
        }
        float ex = nx - MCX, ey = ny - MCY, ed = sqrtf(ex * ex + ey * ey);
        if (ed > BOUND_R) { nx = MCX + ex / ed * BOUND_R; ny = MCY + ey / ed * BOUND_R; vx *= 0.3f; vy *= 0.3f; }
        bx = nx; by = ny;
    }
    lv_obj_set_pos(g_ball, (int)bx - BALL_R, (int)by - BALL_R);

    int gx = OX + (N - 1) * CELL + CELL / 2, gy = OY + (N - 1) * CELL + CELL / 2;
    float ggx = bx - gx, ggy = by - gy;
    if (sqrtf(ggx * ggx + ggy * ggy) < 16) { lv_label_set_text(g_msg, "nice!"); s_win = 18; }
}

static void maze_exit(void) { g_wallbox = g_ball = g_msg = NULL; }

const app_t app_maze = { "maze", COL_TXT, maze_enter, maze_tick, maze_exit };
