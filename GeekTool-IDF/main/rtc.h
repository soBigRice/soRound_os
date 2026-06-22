#pragma once
// PCF85063 RTC(I2C 0x51)—— 断电走时。开机用它恢复系统时间;SNTP 校时后写回。
#include <stdbool.h>

void rtc_begin(void);              // 挂 PCF85063(用 board_i2c_bus);名字避开 IDF 内部的 rtc_init
bool rtc_sync_to_system(void);     // 读 RTC → settimeofday(开机调);RTC 无效/没设过返回 false
void rtc_save_from_system(void);   // 系统时间 → 写回 RTC(SNTP 校时回调里调)
