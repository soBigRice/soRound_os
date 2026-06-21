#pragma once
// AXP2101 电源读取(I2C 0x34)—— 电量 + 充/放电状态。
// 寄存器定义取自小智 main/boards/common/axp2101.cc(本板已验证)。
#include <stdbool.h>

typedef enum {
    PWR_UNKNOWN = 0,
    PWR_DISCHARGING,   // 用电(电池供电)
    PWR_CHARGING,      // 充电中
    PWR_FULL,          // 充满(充电完成)
} pwr_state_t;

void power_init(void);                        // 用 board_i2c_bus() 挂 AXP2101
bool power_read(int *soc, pwr_state_t *st);   // soc:0-100;读失败返回 false(不改 *soc/*st)
void power_off(void);                         // AXP2101 关机(reg 0x10 bit0)
void power_key_init(void);                    // 使能 AXP2101 PWRON 键短/长按 IRQ
int  power_key_event(void);                   // 轮询:0=无 / 1=短按 / 2=长按(读后自动清)
void power_audio_on(void);                    // 使能 AXP2101 ALDO1(3.3V)给音频 codec(ES7210)供电
