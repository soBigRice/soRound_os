#pragma once
// 扬声器输出 —— ES8311(I2S TX,master)经 esp_codec_dev。给倒计时闹铃 + 音量预览提示音用。
// 与麦克风(ES7210/RX)共用 I2S0,但二者分属不同 app、不同时占用:各自 enter 请求启动 / exit 请求停止,后台任务串行交接。
#include <stdbool.h>
#include <stdint.h>

void audio_out_init(void);              // 异步请求 I2S TX + ES8311(幂等);失败记录日志
void audio_out_deinit(void);            // 异步停止并释放;写入结束后才把 I2S0 让回麦克风
void audio_out_set_volume(uint8_t v);   // 0-100(硬件音量)
void audio_out_alarm(void);             // 倒计时闹铃和弦(静音模式或未初始化则不响)
void audio_out_blip(void);              // 短提示音(音量预览用)
void audio_out_knock(void);             // 木鱼空腔敲击;连敲替换余音,共用异步输出与静音/音量约束

// 合成环境声共用原输出任务，闹铃优先。 / Synthesized ambience shares the worker; alarms take priority.
typedef enum { AUDIO_SCENE_RAIN=1, AUDIO_SCENE_OCEAN, AUDIO_SCENE_FIRE } audio_scene_t;
bool audio_out_scene_start(audio_scene_t scene);
void audio_out_scene_stop(void);
bool audio_out_scene_active(void);
// 无常驻客户端的到期提醒，播完自动释放。 / Standalone reminder releases the bus when finished.
bool audio_out_alarm_once(void);

typedef enum { AUDIO_WAV_IDLE, AUDIO_WAV_QUEUED, AUDIO_WAV_PLAYING,
               AUDIO_WAV_DONE, AUDIO_WAV_CANCELLED, AUDIO_WAV_FAILED } audio_wav_state_t;
// 仅接受胶囊目录内的规范文件名；16kHz 单声道 PCM16，最长120秒。
// Accept only canonical capsule paths; 16kHz mono PCM16, at most 120 seconds.
bool audio_out_wav_start(const char *path);
void audio_out_wav_stop(void);
audio_wav_state_t audio_out_wav_state(void);
