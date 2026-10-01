// 音频可视化 app —— 麦克风拾音,Goertzel 算 18 个频段能量,画成【圆形径向爆发】:
// 中心向外 36 根辐条(18 段镜像 → 左右对称),辐条点亮的点数 = 该段能量;内→外 青→黄→红,
// 中心一个红核随总能量脉动。采集+分析在独立任务里(只写 s_band[]);UI 在 audio_tick 更新点阵。
#include "app.h"
#include "audio_mic.h"
#include "audio_bus.h"
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

static float              s_band[NB];          // 0..1 平滑后的频段能量
static volatile bool      s_run;
static TaskHandle_t       s_worker;
static portMUX_TYPE       s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool      s_visible = true;
static uint32_t           s_generation;
static lv_obj_t          *g_line[SPOKES];
static lv_obj_t          *g_core;              // 中心脉冲核
static lv_point_precise_t s_pts[SPOKES][2];    // 每条 2 点(内端固定,外端随能量)
static float              s_ca[SPOKES], s_sa[SPOKES];

static void audio_task(void *arg) {
    (void)arg;
    bool owns_bus = false;
    uint32_t failed_generation = UINT32_MAX;
    static int16_t buf[WIN];
    float coeff[NB];
    for (int b = 0; b < NB; b++) {                          // 频率 80Hz..6kHz 对数分布
        float f = 80.0f * powf(6000.0f / 80.0f, (float)b / (NB - 1));
        coeff[b] = 2.0f * cosf(2.0f * (float)M_PI * f / SR);
    }
    int logdiv = 0;
    for (;;) {
        portENTER_CRITICAL(&s_mux);
        bool run = s_run;
        uint32_t generation = s_generation;
        portEXIT_CRITICAL(&s_mux);
        if (!run) {
            if (owns_bus) { audio_mic_stop(); audio_bus_release(); owns_bus = false; }
            portENTER_CRITICAL(&s_mux);
            bool stop = !s_run;
            if (stop) s_worker = NULL;
            portEXIT_CRITICAL(&s_mux);
            if (stop) { vTaskDelete(NULL); return; }
            continue;
        }
        if (!owns_bus) {
            if (failed_generation == generation) { ulTaskNotifyTake(pdTRUE, portMAX_DELAY); continue; }
            if (!audio_bus_acquire(pdMS_TO_TICKS(50))) continue;
            if (!audio_mic_start(board_i2c_bus())) {
                ESP_LOGW("audio", "mic start failed");
                audio_mic_stop(); audio_bus_release(); failed_generation = generation;
                continue;
            }
            owns_bus = true;
        }
        if (!s_visible) { ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(80)); continue; }
        int n = audio_mic_read(buf, WIN);
        if (n <= 0) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }

        float rms = 0;
        for (int i = 0; i < n; i++) rms += (float)buf[i] * buf[i];
        rms = sqrtf(rms / n);

        float bands[NB];
        portENTER_CRITICAL(&s_mux); memcpy(bands, s_band, sizeof bands); portEXIT_CRITICAL(&s_mux);
        for (int b = 0; b < NB; b++) {                      // Goertzel:每段一个频点的幅度
            float s0, s1 = 0, s2 = 0;
            for (int i = 0; i < n; i++) { s0 = buf[i] / 32768.0f + coeff[b] * s1 - s2; s2 = s1; s1 = s0; }
            float mag = sqrtf(s1 * s1 + s2 * s2 - coeff[b] * s1 * s2) / (n * 0.5f);
            float v = mag * 11.0f; if (v > 1.0f) v = 1.0f;     // 放大灵敏度,起伏更明显
            float prev = bands[b];
            bands[b] = v > prev ? v : prev * 0.80f + v * 0.20f;  // 快上慢下 → 起伏感
        }
        portENTER_CRITICAL(&s_mux); memcpy(s_band, bands, sizeof bands); portEXIT_CRITICAL(&s_mux);
        if (++logdiv >= 15) { logdiv = 0; ESP_LOGI("audio", "rms=%.0f", rms); }  // 拾音验证
    }
}

static void audio_notify(void) {
    portENTER_CRITICAL(&s_mux);
    if (s_worker) xTaskNotifyGive(s_worker);
    portEXIT_CRITICAL(&s_mux);
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
    // 采集任务拥有硬件,结束后释放任务栈;进退页面只发请求,不等待阻塞的 I2S read。
    portENTER_CRITICAL(&s_mux);
    memset(s_band, 0, sizeof s_band); s_run = true; s_generation++;
    portEXIT_CRITICAL(&s_mux);
    s_visible = true;
    if (!s_worker && xTaskCreate(audio_task, "audio", 4096, NULL, 5, &s_worker) != pdPASS) {
        s_run = false; ESP_LOGE("audio", "capture task allocation failed");
    }
    audio_notify();
}

static void audio_tick(void) {
    if (!g_line[0]) return;
    float bands[NB];
    portENTER_CRITICAL(&s_mux); memcpy(bands, s_band, sizeof bands); portEXIT_CRITICAL(&s_mux);
    float sum = 0;
    for (int s = 0; s < SPOKES; s++) {
        int b = (s <= SPOKES / 2) ? s : SPOKES - s;          // 镜像 → 左右对称
        if (b >= NB) b = NB - 1;
        float v = bands[b];
        sum += v;
        int len = R0 + (int)(v * LMAX);
        lv_point_precise_t end = {0};
        end.x = (lv_value_precise_t)(CX + s_ca[s] * len);
        end.y = (lv_value_precise_t)(CY + s_sa[s] * len);
        if (s_pts[s][1].x != end.x || s_pts[s][1].y != end.y) {
            s_pts[s][1] = end; lv_line_set_points(g_line[s], s_pts[s], 2);
        }
        lv_color_t color = lv_color_hex(tier_color(v));
        if (!lv_color_eq(lv_obj_get_style_line_color(g_line[s], 0), color))
            lv_obj_set_style_line_color(g_line[s], color, 0);
    }
    int cr = 7 + (int)(sum / SPOKES * 26.0f);                // 中心核随总能量脉动
    lv_obj_set_size(g_core, cr * 2, cr * 2);
    lv_obj_set_pos(g_core, CX - cr, CY - cr);
}

static void audio_exit(void) {
    portENTER_CRITICAL(&s_mux); s_run = false; s_generation++; portEXIT_CRITICAL(&s_mux);
    audio_notify();
    for (int s = 0; s < SPOKES; s++) g_line[s] = NULL;
    g_core = NULL;
}

static void audio_visibility(bool visible) {
    s_visible = visible;
    audio_notify();
}

const app_t app_audio = { "audio", COL_TXT, audio_enter, audio_tick, audio_exit, NULL, 20, audio_visibility };
