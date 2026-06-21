// 麦克风采集 —— ES7210(4 路 TDM)+ I2S RX(master),经 esp_codec_dev 读单声道 16bit。
// I2S 配置用默认宏(跨 IDF 版本字段稳),codec 链路对齐小智 BoxAudioCodec 的输入侧。
#include "audio_mic.h"
#include "board_config.h"
#include "driver/i2s_tdm.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "power.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG       "mic"
#define MIC_RATE  16000
// ES7210 I2C 地址:这里要传【8 位写地址】,esp_codec_dev 的 i2c ctrl 内部会 >>1 取 7 位
// (audio_codec_ctrl_i2c.c: device_address = addr >> 1)。0x80>>1=0x40 才是 ES7210。
// 之前误填 0x40 → 实际寻到 7 位 0x20(TCA9554 IO 扩展)→ ES7210 从未配置 → 无音频数据,竖条静止。
#define ES7210_ADDR ES7210_CODEC_DEFAULT_ADDR   // = 0x80

static i2s_chan_handle_t       s_rx;
static esp_codec_dev_handle_t  s_dev;
static const audio_codec_data_if_t *s_data_if;
static const audio_codec_ctrl_if_t *s_ctrl_if;
static const audio_codec_if_t      *s_codec_if;

bool audio_mic_start(i2c_master_bus_handle_t i2c_bus) {
    power_audio_on();                    // ES7210 由 AXP2101 ALDO1 供电,先上电
    vTaskDelay(pdMS_TO_TICKS(60));        // 等 codec 上电稳定再配寄存器

    // 1) I2S RX 通道(master,只收)
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    if (i2s_new_channel(&chan, NULL, &s_rx) != ESP_OK) { ESP_LOGE(TAG, "i2s_new_channel"); return false; }

    i2s_tdm_config_t tdm = {
        .clk_cfg  = I2S_TDM_CLK_DEFAULT_CONFIG(MIC_RATE),
        .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO,
                        I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3),
        .gpio_cfg = {
            .mclk = AUDIO_MCLK, .bclk = AUDIO_BCLK, .ws = AUDIO_WS,
            .dout = I2S_GPIO_UNUSED, .din = AUDIO_DIN,
        },
    };
    if (i2s_channel_init_tdm_mode(s_rx, &tdm) != ESP_OK) { ESP_LOGE(TAG, "i2s tdm init"); return false; }

    // 2) esp_codec_dev:I2S 数据接口 + ES7210 的 I2C 控制接口
    audio_codec_i2s_cfg_t i2s_if_cfg = { .port = I2S_NUM_0, .rx_handle = s_rx, .tx_handle = NULL };
    s_data_if = audio_codec_new_i2s_data(&i2s_if_cfg);
    audio_codec_i2c_cfg_t i2c_cfg = { .port = 0, .addr = ES7210_ADDR, .bus_handle = i2c_bus };
    s_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!s_data_if || !s_ctrl_if) { ESP_LOGE(TAG, "codec if"); return false; }

    es7210_codec_cfg_t es_cfg = {
        .ctrl_if = s_ctrl_if,
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4,
    };
    s_codec_if = es7210_codec_new(&es_cfg);
    if (!s_codec_if) { ESP_LOGE(TAG, "es7210 new"); return false; }

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN, .codec_if = s_codec_if, .data_if = s_data_if,
    };
    s_dev = esp_codec_dev_new(&dev_cfg);
    if (!s_dev) { ESP_LOGE(TAG, "codec_dev_new"); return false; }

    esp_codec_dev_sample_info_t fs = { .bits_per_sample = 16, .channel = 1, .sample_rate = MIC_RATE };
    if (esp_codec_dev_open(s_dev, &fs) != ESP_CODEC_DEV_OK) { ESP_LOGE(TAG, "codec_dev_open"); return false; }
    esp_codec_dev_set_in_channel_gain(s_dev, ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0), 40.0f);  // 增益 40dB(更灵敏)
    ESP_LOGI(TAG, "mic started @ %dHz", MIC_RATE);
    return true;
}

int audio_mic_read(int16_t *buf, int samples) {
    if (!s_dev) return 0;
    if (esp_codec_dev_read(s_dev, buf, samples * sizeof(int16_t)) == ESP_CODEC_DEV_OK) return samples;
    return 0;
}

void audio_mic_stop(void) {
    if (s_dev) { esp_codec_dev_close(s_dev); esp_codec_dev_delete(s_dev); s_dev = NULL; }
    if (s_codec_if) { audio_codec_delete_codec_if(s_codec_if); s_codec_if = NULL; }
    if (s_ctrl_if)  { audio_codec_delete_ctrl_if(s_ctrl_if);   s_ctrl_if  = NULL; }
    if (s_data_if)  { audio_codec_delete_data_if(s_data_if);   s_data_if  = NULL; }
    if (s_rx) { i2s_channel_disable(s_rx); i2s_del_channel(s_rx); s_rx = NULL; }   // 显式 disable 再删,确保 DMA 缓冲释放(已 disable 时返回错误,忽略)
}
