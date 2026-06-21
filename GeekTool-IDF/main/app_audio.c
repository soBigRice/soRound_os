// 音频可视化 app —— 麦克风拾音,Goertzel 算 18 个频段能量,画成高低起伏的彩色竖条。
// 采集+分析在独立任务里(只写 s_band[]);UI 在 audio_tick(LVGL 任务)读 s_band 更新竖条。
#include "app.h"
#include "audio_mic.h"
#include "board_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

#define NB    18           // 频段数 = 竖条数
#define WIN   480          // 每帧样本数(16kHz → ~33fps)
#define SR    16000.0f
#define BASE_Y 392         // 竖条底边 y
#define BAR_W  14
#define BAR_G  5
#define HMAX   312         // 最大条高

static volatile float s_band[NB];     // 0..1 平滑后的频段能量
static volatile bool  s_run;
static lv_obj_t      *g_bar[NB];

static void audio_task(void *arg) {
    if (!audio_mic_start(board_i2c_bus())) {
        ESP_LOGW("audio", "mic start failed — 竖条静止");
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
            float v = mag * 7.0f; if (v > 1.0f) v = 1.0f;
            float prev = s_band[b];
            s_band[b] = v > prev ? v : prev * 0.80f + v * 0.20f;  // 快上慢下 → 起伏感
        }
        if (++logdiv >= 15) { logdiv = 0; ESP_LOGI("audio", "rms=%.0f", rms); }  // 拾音验证
    }
    audio_mic_stop();
    vTaskDelete(NULL);
}

static void audio_enter(lv_obj_t *parent) {
    int total = NB * BAR_W + (NB - 1) * BAR_G;
    int x0 = (LCD_H_RES - total) / 2;
    for (int b = 0; b < NB; b++) {
        lv_obj_t *bar = lv_obj_create(parent);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, BAR_W, 6);
        lv_obj_set_style_radius(bar, 3, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_pos(bar, x0 + b * (BAR_W + BAR_G), BASE_Y - 6);
        g_bar[b] = bar;
    }
    memset((void *)s_band, 0, sizeof(s_band));
    s_run = true;
    xTaskCreate(audio_task, "audio", 4096, NULL, 5, NULL);   // 麦克风在任务里起(含上电延时,不卡 LVGL)
}

static void audio_tick(void) {
    if (!g_bar[0]) return;
    for (int b = 0; b < NB; b++) {
        float v = s_band[b];
        int h = 6 + (int)(v * HMAX);
        lv_obj_set_height(g_bar[b], h);
        lv_obj_set_y(g_bar[b], BASE_Y - h);
        uint32_t col = v < 0.45f ? 0x36e0c0 : (v < 0.75f ? 0xffd24a : 0xff4d4d);  // 低青·中黄·高红
        lv_obj_set_style_bg_color(g_bar[b], lv_color_hex(col), 0);
    }
}

static void audio_exit(void) {
    s_run = false;                       // 任务自己收尾(停麦+自删),避免 use-after-free
    for (int b = 0; b < NB; b++) g_bar[b] = NULL;
}

const app_t app_audio = { "audio", COL_TXT, audio_enter, audio_tick, audio_exit };
