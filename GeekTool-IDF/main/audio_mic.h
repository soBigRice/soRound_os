#pragma once
// 麦克风采集:ES7210(4 路 TDM)+ I2S,经 esp_codec_dev。给音频可视化 app 用。
#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"

bool audio_mic_start(i2c_master_bus_handle_t i2c_bus);  // 起 I2S + ES7210,成功返回 true
int  audio_mic_read(int16_t *buf, int samples);         // 读单声道 16bit 样本,返回读到的样本数
void audio_mic_stop(void);                              // 关闭并释放(由采集任务自己调)
