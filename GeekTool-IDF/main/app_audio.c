// 音频可视化 app —— 麦克风拾音,Goertzel 算 18 个频段能量,画成【圆形径向爆发】:
// 中心向外 36 根辐条(18 段镜像 → 左右对称),辐条点亮的点数 = 该段能量;内→外 青→黄→红,
// 中心一个红核随总能量脉动。采集+分析在独立任务里(只写 s_band[]);UI 在 audio_tick 更新点阵。
#include "app.h"
#include "audio_mic.h"
#include "board_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

#define NB     18          // 频段数
#define WIN    480         // 每帧样本数(16kHz → ~33fps)
#define SR     16000.0f

// 径向布局(466 圆屏,中心 233,233):中心向外的实心辐条(粗圆头线),长度=该段能量
#define CX     233
#define CY     233
#define SPOKES 36          // 辐条数(18 段镜像 → 左右对称)
#define R0     58          // 内半径(起点)
#define LMAX   150         // 最大伸出长度
#define LINE_W 9           // 辐条粗细(实心圆头,比点阵醒目)
#define COL_LO 0x21e6b6    // 低 青
#define COL_MD 0xffc233    // 中 黄
#define COL_HI 0xff3b3b    // 高 红

static volatile float     s_band[NB];          // 0..1 平滑后的频段能量
static volatile bool      s_run;
static volatile bool      s_task_alive;        // 采集任务在跑?防快速重进时两个任务并存抢 I2S0
static lv_obj_t          *g_line[SPOKES];
static lv_obj_t          *g_core;              // 中心脉冲核
static lv_point_precise_t s_pts[SPOKES][2];    // 每条 2 点(内端固定,外端随能量)
static float              s_ca[SPOKES], s_sa[SPOKES];

static void audio_task(void *arg) {
    s_task_alive = true;
    if (!audio_mic_start(board_i2c_bus())) {
        ESP_LOGW("audio", "mic start failed — 竖条静止");
        s_task_alive = false;
        vTaskDelete(NULL);
        return;
    }
    static int16_t buf[WIN];
    float coeff[NB];
    for (int b = 0; b < NB; b++) {                          // 频率 80Hz..6kHz 对数分布
        float f = 80.0f * powf(6000.0f / 80.0f, (float)b / (NB - 1));
        coeff[b] = 2.0f * cosf(2.0f * (float)M_PI * f / SR);
    }
    int logdiv = 0;
    while (s_run) {
        int n = audio_mic_read(buf, WIN);
        if (n <= 0) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }

        float rms = 0;
        for (int i = 0; i < n; i++) rms += (float)buf[i] * buf[i];
        rms = sqrtf(rms / n);

        for (int b = 0; b < NB; b++) {                      // Goertzel:每段一个频点的幅度
            float s0, s1 = 0, s2 = 0;
            for (int i = 0; i < n; i++) { s0 = buf[i] / 32768.0f + coeff[b] * s1 - s2; s2 = s1; s1 = s0; }
            float mag = sqrtf(s1 * s1 + s2 * s2 - coeff[b] * s1 * s2) / (n * 0.5f);
            float v = mag * 11.0f; if (v > 1.0f) v = 1.0f;     // 放大灵敏度,起伏更明显
            float prev = s_band[b];
            s_band[b] = v > prev ? v : prev * 0.80f + v * 0.20f;  // 快上慢下 → 起伏感
        }
        if (++logdiv >= 15) { logdiv = 0; ESP_LOGI("audio", "rms=%.0f", rms); }  // 拾音验证
    }
    audio_mic_stop();
    s_task_alive = false;
    vTaskDelete(NULL);
}

static uint32_t tier_color(float v) { return v < 0.35f ? COL_LO : (v < 0.7f ? COL_MD : COL_HI); }

static void audio_enter(lv_obj_t *parent) {
    for (int s = 0; s < SPOKES; s++) {
        float a = s * (6.2831853f / SPOKES) - 1.5708f;       // 从正上方起,顺时针
        s_ca[s] = cosf(a); s_sa[s] = sinf(a);
        s_pts[s][0].x = (lv_value_precise_t)(CX + s_ca[s] * R0);   // 内端固定
        s_pts[s][0].y = (lv_value_precise_t)(CY + s_sa[s] * R0);
        s_pts[s][1] = s_pts[s][0];                            // 初始零长
        lv_obj_t *ln = lv_line_create(parent);
        lv_obj_set_style_line_width(ln, LINE_W, 0);
        lv_obj_set_style_line_rounded(ln, true, 0);
        lv_obj_set_style_line_color(ln, lv_color_hex(COL_LO), 0);
        lv_obj_add_flag(ln, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_line_set_points(ln, s_pts[s], 2);
        g_line[s] = ln;
    }
    g_core = lv_obj_create(parent);
    lv_obj_remove_style_all(g_core);
    lv_obj_set_style_radius(g_core, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_core, lv_color_hex(COL_HI), 0);
    lv_obj_set_style_bg_opa(g_core, LV_OPA_COVER, 0);
    lv_obj_remove_flag(g_core, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_core, LV_OBJ_FLAG_EVENT_BUBBLE);
    // 防快速重进:确保上一个采集任务已退出,再起新的(否则两个任务抢 I2S0/全局,偶发卡死)
    s_run = false;
    for (int i = 0; i < 40 && s_task_alive; i++) vTaskDelay(pdMS_TO_TICKS(10));
    memset((void *)s_band, 0, sizeof(s_band));
    s_run = true;
    xTaskCreate(audio_task, "audio", 4096, NULL, 5, NULL);   // 麦克风在任务里起(含上电延时,不卡 LVGL)
}

static void audio_tick(void) {
    if (!g_line[0]) return;
    float sum = 0;
    for (int s = 0; s < SPOKES; s++) {
        int b = (s <= SPOKES / 2) ? s : SPOKES - s;          // 镜像 → 左右对称
        if (b >= NB) b = NB - 1;
        float v = s_band[b];
        sum += v;
        int len = R0 + (int)(v * LMAX);
        s_pts[s][1].x = (lv_value_precise_t)(CX + s_ca[s] * len);
        s_pts[s][1].y = (lv_value_precise_t)(CY + s_sa[s] * len);
        lv_line_set_points(g_line[s], s_pts[s], 2);
        lv_obj_set_style_line_color(g_line[s], lv_color_hex(tier_color(v)), 0);
    }
    int cr = 7 + (int)(sum / SPOKES * 26.0f);                // 中心核随总能量脉动
    lv_obj_set_size(g_core, cr * 2, cr * 2);
    lv_obj_set_pos(g_core, CX - cr, CY - cr);
}

static void audio_exit(void) {
    s_run = false;                       // 任务自己收尾(停麦+自删),避免 use-after-free
    for (int s = 0; s < SPOKES; s++) g_line[s] = NULL;
    g_core = NULL;
}

const app_t app_audio = { "audio", COL_TXT, audio_enter, audio_tick, audio_exit };
