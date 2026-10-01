// 扬声器输出 —— 见 audio_out.h。ES8311 配置对齐小智 BoxAudioCodec 的输出侧(DAC + MCLK + PA)。
// 闹铃/提示音都是代码合成的正弦音(带指数衰减,像"叮"),免解码器/免资源文件。
// 播放在独立任务里跑(esp_codec_dev_write 阻塞),不卡 LVGL。
#include "audio_out.h"
#include "audio_bus.h"
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
static TaskHandle_t s_worker;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_wanted, s_busy;
static uint32_t s_generation, s_play_generation;
static int s_req;
static uint8_t s_volume, s_applied_volume = UINT8_MAX;
static bool s_io_failed;

static bool cancelled(void) {
    portENTER_CRITICAL(&s_mux);
    bool cancel = !s_wanted || s_generation != s_play_generation;
    portEXIT_CRITICAL(&s_mux);
    return cancel || s_io_failed;
}

static void hardware_deinit(void) {
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

static void hardware_init(void) {
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
    if (i2s_channel_init_std_mode(s_tx, &std) != ESP_OK) { ESP_LOGE(TAG, "i2s std init"); hardware_deinit(); return; }

    audio_codec_i2s_cfg_t i2s_if_cfg = { .port = I2S_NUM_0, .rx_handle = NULL, .tx_handle = s_tx };
    s_data_if = audio_codec_new_i2s_data(&i2s_if_cfg);
    audio_codec_i2c_cfg_t i2c_cfg = { .port = 0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = board_i2c_bus() };
    s_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);     // 8 位地址,ctrl 内部 >>1(见 [[esp-codec-dev-i2c-addr]])
    s_gpio_if = audio_codec_new_gpio();
    if (!s_data_if || !s_ctrl_if || !s_gpio_if) { ESP_LOGE(TAG, "codec if"); hardware_deinit(); return; }

    es8311_codec_cfg_t es = {
        .ctrl_if     = s_ctrl_if,
        .gpio_if     = s_gpio_if,
        .codec_mode  = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin      = AUDIO_PA,
        .use_mclk    = true,
        .hw_gain     = { .pa_voltage = 5.0f, .codec_dac_voltage = 3.3f },
    };
    s_codec_if = es8311_codec_new(&es);
    if (!s_codec_if) { ESP_LOGE(TAG, "es8311 new"); hardware_deinit(); return; }

    esp_codec_dev_cfg_t dc = { .dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = s_codec_if, .data_if = s_data_if };
    s_dev = esp_codec_dev_new(&dc);
    if (!s_dev) { ESP_LOGE(TAG, "dev new"); hardware_deinit(); return; }

    esp_codec_dev_sample_info_t fs = { .bits_per_sample = 16, .channel = 1, .sample_rate = RATE };
    if (esp_codec_dev_open(s_dev, &fs) != ESP_CODEC_DEV_OK) { ESP_LOGE(TAG, "open"); hardware_deinit(); return; }
    ESP_LOGI(TAG, "output ready");
}

static void apply_volume(void) {
    portENTER_CRITICAL(&s_mux); uint8_t volume = s_volume; portEXIT_CRITICAL(&s_mux);
    if (volume != s_applied_volume) {
        if (esp_codec_dev_set_out_vol(s_dev, volume) == ESP_CODEC_DEV_OK) s_applied_volume = volume;
        else { ESP_LOGW(TAG, "volume update failed"); s_io_failed = true; }
    }
}
static void notify_worker(void) {
    portENTER_CRITICAL(&s_mux);
    if (s_worker) xTaskNotifyGive(s_worker);
    portEXIT_CRITICAL(&s_mux);
}

/* ---- 合成音 ---- */
static int16_t s_buf[CHUNK];

static void write_tone(float freq, int ms, float amp) {
    int n = RATE * ms / 1000, done = 0;
    float ph = 0, dph = 6.2831853f * freq / RATE;
    while (done < n) {
        if (cancelled()) return;
        apply_volume();
        if (s_io_failed) return;
        int k = (n - done < CHUNK) ? (n - done) : CHUNK;
        for (int i = 0; i < k; i++) {
            float env = expf(-3.2f * (float)(done + i) / n);     // 指数衰减 → "叮"
            s_buf[i] = (int16_t)(sinf(ph) * amp * env * 32000.0f);
            ph += dph; if (ph > 6.2831853f) ph -= 6.2831853f;
        }
        if (esp_codec_dev_write(s_dev, s_buf, k * sizeof(int16_t)) != ESP_CODEC_DEV_OK) {
            ESP_LOGW(TAG, "I2S write failed"); s_io_failed = true; return;
        }
        done += k;
    }
}
static void write_silence(int ms) {
    int n = RATE * ms / 1000, done = 0;
    memset(s_buf, 0, sizeof s_buf);
    while (done < n) {
        if (cancelled()) return;
        apply_volume();
        if (s_io_failed) return;
        int k = (n - done < CHUNK) ? (n - done) : CHUNK;
        if (esp_codec_dev_write(s_dev, s_buf, k * sizeof(int16_t)) != ESP_CODEC_DEV_OK) {
            ESP_LOGW(TAG, "I2S write failed"); s_io_failed = true; return;
        }
        done += k;
    }
}

static void output_worker(void *arg) {
    (void)arg;
    bool owns_bus = false;
    uint32_t failed_generation = UINT32_MAX;
    for (;;) {
        portENTER_CRITICAL(&s_mux);
        bool wanted = s_wanted;
        uint32_t generation = s_generation;
        portEXIT_CRITICAL(&s_mux);
        if (!wanted) {
            if (owns_bus) { hardware_deinit(); audio_bus_release(); owns_bus = false; }
            portENTER_CRITICAL(&s_mux);
            bool stop = !s_wanted;
            if (stop) s_worker = NULL;
            portEXIT_CRITICAL(&s_mux);
            if (stop) { vTaskDelete(NULL); return; }
            continue;
        }
        if (!owns_bus) {
            if (failed_generation == generation) { ulTaskNotifyTake(pdTRUE, portMAX_DELAY); continue; }
            if (!audio_bus_acquire(pdMS_TO_TICKS(50))) continue;
            owns_bus = true;
            hardware_init();
            if (!s_dev) {
                failed_generation = generation;
                hardware_deinit(); audio_bus_release(); owns_bus = false;
                continue;
            }
            s_applied_volume = UINT8_MAX; s_io_failed = false;
        }
        apply_volume();
        portENTER_CRITICAL(&s_mux);
        int req = s_req;
        s_req = 0;
        s_busy = req != 0;
        s_play_generation = s_generation;
        portEXIT_CRITICAL(&s_mux);
        if (req && !settings_silent() && !cancelled()) {
            if (req == 2) {
                for (int r = 0; r < 2 && !cancelled(); r++) {
                    write_tone(1047, 150, 0.6f);
                    write_tone(1319, 150, 0.6f);
                    write_tone(1568, 200, 0.6f);
                    write_silence(150);
                }
            } else write_tone(1175, 80, 0.5f);
        }
        portENTER_CRITICAL(&s_mux);
        s_busy = false;
        portEXIT_CRITICAL(&s_mux);
        if (s_io_failed) {
            hardware_deinit(); audio_bus_release(); owns_bus = false; failed_generation = generation;
        }
        // 播放结束/取消后才释放 codec 和 I2S;绝不在 write 仍阻塞时强行 free。
        ulTaskNotifyTake(pdTRUE, req ? 0 : portMAX_DELAY);
    }
}

void audio_out_init(void) {
    uint8_t volume = settings_volume();
    portENTER_CRITICAL(&s_mux);
    if (!s_wanted) s_generation++;
    s_wanted = true; s_volume = volume;
    portEXIT_CRITICAL(&s_mux);
    if (!s_worker && xTaskCreate(output_worker, "aout", 4096, NULL, 5, &s_worker) != pdPASS) {
        portENTER_CRITICAL(&s_mux); s_wanted = false; portEXIT_CRITICAL(&s_mux);
        ESP_LOGE(TAG, "output task allocation failed"); return;
    }
    notify_worker();
}
void audio_out_deinit(void) {
    portENTER_CRITICAL(&s_mux);
    s_wanted = false; s_req = 0; s_generation++;
    portEXIT_CRITICAL(&s_mux);
    notify_worker();
}
void audio_out_set_volume(uint8_t v) {
    portENTER_CRITICAL(&s_mux);
    s_volume = v > 100 ? 100 : v;
    portEXIT_CRITICAL(&s_mux);
    notify_worker();
}
static void play(int req) {
    if (settings_silent()) return;
    portENTER_CRITICAL(&s_mux);
    if (s_wanted && !s_busy && !s_req) s_req = req;
    portEXIT_CRITICAL(&s_mux);
    notify_worker();
}
void audio_out_alarm(void) { play(2); }
void audio_out_blip(void) { play(1); }
