#pragma once
// 全局设置:屏幕亮度 + 音量 + 锁屏表盘 + 空闲策略。状态在这里持有,存 NVS;UI 在 app_settings.c。
#include <stdint.h>

#define SETTINGS_BRIGHT_MIN 0x40   // 亮度下限 ~25%:再低 AMOLED 看着像黑屏,且防止存了过低值后开机黑屏

// 空闲(锁屏长时间不操作)策略
#define IDLE_AOD 0    // 低功耗长显:变暗 + 不闪 + 按分钟刷新,保持常亮(默认)
#define IDLE_OFF 1    // 自动熄屏:先变暗再黑屏

void    settings_init(void);              // 开机:从 NVS 读 + 应用亮度
uint8_t settings_brightness(void);        // 0x10-0xFF
void    settings_set_brightness(uint8_t v); // 立即应用到屏幕(不写 NVS,拖动时用)
uint8_t settings_volume(void);            // 0-100(暂存,音频未接)
void    settings_set_volume(uint8_t v);
uint8_t settings_face(void);              // 锁屏表盘索引(由 watchface 钳到有效范围)
void    settings_set_face(uint8_t v);     // 仅置 RAM 值;持久化调 settings_save()
uint8_t settings_idle_mode(void);         // IDLE_AOD / IDLE_OFF
void    settings_set_idle_mode(uint8_t v);
uint8_t settings_silent(void);            // 1=静音:闹钟/提示音关闭
void    settings_set_silent(uint8_t v);
void    settings_save(void);              // 把当前值写 NVS(离散改动或滑块松手时调,避免频繁写 flash)
