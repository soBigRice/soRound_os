// 扬声器输出 —— 见 audio_out.h。ES8311 配置对齐小智 BoxAudioCodec 的输出侧(DAC + MCLK + PA)。
// 闹铃/提示音都是代码合成的正弦音(带指数衰减,像"叮"),免解码器/免资源文件。
// 播放在独立任务里跑(esp_codec_dev_write 阻塞),不卡 LVGL。
#include "audio_out.h"
#include "board_config.h"
#include "settings.h"
#include "power.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

#define TAG    "aout"
#define RATE   16000
#define CHUNK  512

static i2s_chan_handle_t            s_tx;
static esp_codec_dev_handle_t       s_dev;
static const audio_codec_data_if_t *s_data_if;
static const audio_codec_ctrl_if_t *s_ctrl_if;
static const audio_codec_gpio_if_t *s_gpio_if;
static const audio_codec_if_t      *s_codec_if;
static volatile bool s_busy;
static volatile bool s_abort;      // 让正在播放的任务尽快退出(避免退出 app 时释放到一半)
static int           s_req;        // 1=blip 2=alarm

void audio_out_deinit(void) {
    s_abort = true;                                            // 通知播放任务停
    for (int i = 0; i < 60 && s_busy; i++) vTaskDelay(pdMS_TO_TICKS(10));  // 等它退出(≤600ms),防 use-after-free
    s_abort = false;
    // esp_codec_dev_close 内部会 disable i2s 通道;若这次没播放过(通道从未被 enable),
    // 它的 disable 落在未启用的通道上,会刷一条无害的 i2s_common "not enabled yet" 错误。teardown 期间压掉该 tag。
    esp_log_level_set("i2s_common", ESP_LOG_NONE);
    if (s_dev)      { esp_codec_dev_close(s_dev); esp_codec_dev_delete(s_dev); s_dev = NULL; }
    if (s_codec_if) { audio_codec_delete_codec_if(s_codec_if); s_codec_if = NULL; }
    if (s_ctrl_if)  { audio_codec_delete_ctrl_if(s_ctrl_if);   s_ctrl_if  = NULL; }
    if (s_gpio_if)  { audio_codec_delete_gpio_if(s_gpio_if);   s_gpio_if  = NULL; }
    if (s_data_if)  { audio_codec_delete_data_if(s_data_if);   s_data_if  = NULL; }
    if (s_tx)       { i2s_del_channel(s_tx); s_tx = NULL; }
    esp_log_level_set("i2s_common", ESP_LOG_INFO);   // 恢复,后续真有 i2s 错误照常打印
}

void audio_out_init(void) {
    if (s_dev) return;
    power_audio_on();                                  // 音频段供电(ALDO1)
    vTaskDelay(pdMS_TO_TICKS(40));

    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    if (i2s_new_channel(&chan, &s_tx, NULL) != ESP_OK) { ESP_LOGE(TAG, "i2s_new_channel"); return; }

    i2s_std_config_t std = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(RATE),   // 默认 MCLK=256fs(ES8311 用 MCLK)
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = { .mclk = AUDIO_MCLK, .bclk = AUDIO_BCLK, .ws = AUDIO_WS,
                      .dout = AUDIO_DOUT, .din = I2S_GPIO_UNUSED },
    };
    if (i2s_channel_init_std_mode(s_tx, &std) != ESP_OK) { ESP_LOGE(TAG, "i2s std init"); audio_out_deinit(); return; }

    audio_codec_i2s_cfg_t i2s_if_cfg = { .port = I2S_NUM_0, .rx_handle = NULL, .tx_handle = s_tx };
    s_data_if = audio_codec_new_i2s_data(&i2s_if_cfg);
    audio_codec_i2c_cfg_t i2c_cfg = { .port = 0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = board_i2c_bus() };
    s_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);     // 8 位地址,ctrl 内部 >>1(见 [[esp-codec-dev-i2c-addr]])
    s_gpio_if = audio_codec_new_gpio();
    if (!s_data_if || !s_ctrl_if || !s_gpio_if) { ESP_LOGE(TAG, "codec if"); audio_out_deinit(); return; }

    es8311_codec_cfg_t es = {
        .ctrl_if     = s_ctrl_if,
        .gpio_if     = s_gpio_if,
        .codec_mode  = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin      = AUDIO_PA,
        .use_mclk    = true,
        .hw_gain     = { .pa_voltage = 5.0f, .codec_dac_voltage = 3.3f },
    };
    s_codec_if = es8311_codec_new(&es);
    if (!s_codec_if) { ESP_LOGE(TAG, "es8311 new"); audio_out_deinit(); return; }

    esp_codec_dev_cfg_t dc = { .dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = s_codec_if, .data_if = s_data_if };
    s_dev = esp_codec_dev_new(&dc);
    if (!s_dev) { ESP_LOGE(TAG, "dev new"); audio_out_deinit(); return; }

    esp_codec_dev_sample_info_t fs = { .bits_per_sample = 16, .channel = 1, .sample_rate = RATE };
    if (esp_codec_dev_open(s_dev, &fs) != ESP_CODEC_DEV_OK) { ESP_LOGE(TAG, "open"); audio_out_deinit(); return; }
    audio_out_set_volume(settings_volume());
    ESP_LOGI(TAG, "output ready");
}

void audio_out_set_volume(uint8_t v) {
    if (s_dev) esp_codec_dev_set_out_vol(s_dev, v);
}

/* ---- 合成音 ---- */
static int16_t s_buf[CHUNK];

static void write_tone(float freq, int ms, float amp) {
    int n = RATE * ms / 1000, done = 0;
    float ph = 0, dph = 6.2831853f * freq / RATE;
    while (done < n) {
        if (s_abort) return;
        int k = (n - done < CHUNK) ? (n - done) : CHUNK;
        for (int i = 0; i < k; i++) {
            float env = expf(-3.2f * (float)(done + i) / n);     // 指数衰减 → "叮"
            s_buf[i] = (int16_t)(sinf(ph) * amp * env * 32000.0f);
            ph += dph; if (ph > 6.2831853f) ph -= 6.2831853f;
        }
        esp_codec_dev_write(s_dev, s_buf, k * sizeof(int16_t));
        done += k;
    }
}
static void write_silence(int ms) {
    int n = RATE * ms / 1000, done = 0;
    memset(s_buf, 0, sizeof s_buf);
    while (done < n) {
        if (s_abort) return;
        int k = (n - done < CHUNK) ? (n - done) : CHUNK;
        esp_codec_dev_write(s_dev, s_buf, k * sizeof(int16_t));
        done += k;
    }
}

static void play_task(void *arg) {
    if (s_dev) {
        if (s_req == 2) {                                 // 闹铃:上行三音和弦,重复两遍
            for (int r = 0; r < 2; r++) {
                write_tone(1047, 150, 0.6f);              // C6
                write_tone(1319, 150, 0.6f);              // E6
                write_tone(1568, 200, 0.6f);              // G6
                write_silence(150);
            }
        } else {                                          // 提示音:一声短"叮"
            write_tone(1175, 80, 0.5f);
        }
    }
    s_busy = false;
    vTaskDelete(NULL);
}

static void play(int req, uint32_t stack) {
    if (settings_silent() || !s_dev || s_busy) return;    // 静音模式 / 未初始化 / 正在播 → 跳过
    s_req = req;
    s_busy = true;
    if (xTaskCreate(play_task, "aplay", stack, NULL, 5, NULL) != pdPASS) s_busy = false;
}

void audio_out_alarm(void) { play(2, 4096); }
void audio_out_blip(void)  { play(1, 3072); }
