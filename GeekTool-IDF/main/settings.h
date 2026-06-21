#pragma once
// 全局设置:屏幕亮度 + 音量。状态在这里持有,存 NVS;UI 在 app_settings.c。
#include <stdint.h>

#define SETTINGS_BRIGHT_MIN 0x40   // 亮度下限 ~25%:再低 AMOLED 看着像黑屏,且防止存了过低值后开机黑屏

void    settings_init(void);              // 开机:从 NVS 读 + 应用亮度
uint8_t settings_brightness(void);        // 0x10-0xFF
void    settings_set_brightness(uint8_t v); // 立即应用到屏幕(不写 NVS,拖动时用)
uint8_t settings_volume(void);            // 0-100(暂存,音频未接)
void    settings_set_volume(uint8_t v);
void    settings_save(void);              // 把当前值写 NVS(滑块松手时调,避免频繁写 flash)
