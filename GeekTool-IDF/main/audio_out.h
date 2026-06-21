#pragma once
// 扬声器输出 —— ES8311(I2S TX,master)经 esp_codec_dev。给倒计时闹铃 + 音量预览提示音用。
// 与麦克风(ES7210/RX)共用 I2S0,但二者分属不同 app、不同时占用:各自 enter 起 / exit 停。
#include <stdbool.h>
#include <stdint.h>

void audio_out_init(void);              // 起 I2S TX + ES8311(幂等);失败则后续播放静默
void audio_out_deinit(void);            // 释放(app 退出时调,把 I2S0 让回麦克风)
void audio_out_set_volume(uint8_t v);   // 0-100(硬件音量)
void audio_out_alarm(void);             // 倒计时闹铃和弦(静音模式或未初始化则不响)
void audio_out_blip(void);              // 短提示音(音量预览用)
