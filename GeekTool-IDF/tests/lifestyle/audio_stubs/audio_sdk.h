#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef void *TaskHandle_t;
typedef unsigned TickType_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
#define pdMS_TO_TICKS(ms) (ms)
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define ESP_OK 0
#define ESP_CODEC_DEV_OK 0
#define ESP_LOG_NONE 0
#define ESP_LOG_INFO 1
#define ESP_LOGI(tag,...) ((void)(tag))
#define ESP_LOGW(tag,...) ((void)(tag))
#define ESP_LOGE(tag,...) ((void)(tag))
int xTaskCreate(void (*)(void *),const char *,unsigned,void *,unsigned,void *);
void vTaskDelete(void *);
void vTaskDelay(unsigned);
void xTaskNotifyGive(TaskHandle_t);
unsigned ulTaskNotifyTake(int,unsigned);
void esp_log_level_set(const char *,int);

typedef void *i2s_chan_handle_t;
typedef struct {unsigned dma_desc_num,dma_frame_num;bool auto_clear_after_cb,auto_clear_before_cb;} i2s_chan_config_t;
#define I2S_NUM_0 0
#define I2S_ROLE_MASTER 0
#define I2S_DATA_BIT_WIDTH_16BIT 16
#define I2S_SLOT_MODE_STEREO 2
#define I2S_GPIO_UNUSED -1
#define I2S_CHANNEL_DEFAULT_CONFIG(a,b) ((i2s_chan_config_t){.dma_desc_num=6,.dma_frame_num=240})
#define I2S_STD_CLK_DEFAULT_CONFIG(a) 0
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(a,b) 0
typedef struct {int clk_cfg,slot_cfg;struct {int mclk,bclk,ws,dout,din;} gpio_cfg;} i2s_std_config_t;
int i2s_new_channel(const i2s_chan_config_t *,i2s_chan_handle_t *,void *);
int i2s_channel_init_std_mode(i2s_chan_handle_t,const i2s_std_config_t *);
int i2s_del_channel(i2s_chan_handle_t);

typedef void *esp_codec_dev_handle_t;
typedef struct {int unused;} audio_codec_data_if_t;
typedef audio_codec_data_if_t audio_codec_ctrl_if_t;
typedef audio_codec_data_if_t audio_codec_gpio_if_t;
typedef audio_codec_data_if_t audio_codec_if_t;
typedef struct {int port;void *rx_handle,*tx_handle;} audio_codec_i2s_cfg_t;
typedef struct {int port,addr;void *bus_handle;} audio_codec_i2c_cfg_t;
typedef struct {
    const audio_codec_ctrl_if_t *ctrl_if;const audio_codec_gpio_if_t *gpio_if;
    int codec_mode,pa_pin;bool use_mclk;struct {float pa_voltage,codec_dac_voltage;} hw_gain;
} es8311_codec_cfg_t;
typedef struct {int dev_type;const audio_codec_if_t *codec_if;const audio_codec_data_if_t *data_if;} esp_codec_dev_cfg_t;
typedef struct {int bits_per_sample,channel,sample_rate;} esp_codec_dev_sample_info_t;
#define ES8311_CODEC_DEFAULT_ADDR 0x30
#define ESP_CODEC_DEV_WORK_MODE_DAC 1
#define ESP_CODEC_DEV_TYPE_OUT 1
const audio_codec_data_if_t *audio_codec_new_i2s_data(const audio_codec_i2s_cfg_t *);
const audio_codec_ctrl_if_t *audio_codec_new_i2c_ctrl(const audio_codec_i2c_cfg_t *);
const audio_codec_gpio_if_t *audio_codec_new_gpio(void);
const audio_codec_if_t *es8311_codec_new(const es8311_codec_cfg_t *);
esp_codec_dev_handle_t esp_codec_dev_new(const esp_codec_dev_cfg_t *);
int esp_codec_dev_open(esp_codec_dev_handle_t,const esp_codec_dev_sample_info_t *);
int esp_codec_dev_close(esp_codec_dev_handle_t);
void esp_codec_dev_delete(esp_codec_dev_handle_t);
void audio_codec_delete_codec_if(const audio_codec_if_t *);
void audio_codec_delete_ctrl_if(const audio_codec_ctrl_if_t *);
void audio_codec_delete_gpio_if(const audio_codec_gpio_if_t *);
void audio_codec_delete_data_if(const audio_codec_data_if_t *);
int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t,unsigned);
int esp_codec_dev_write(esp_codec_dev_handle_t,void *,int);
