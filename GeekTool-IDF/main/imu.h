#pragma once
// QMI8658 六轴 IMU(I2C 0x6b / 0x6a)—— 只用加速度计。给水平仪、重力迷宫小球用。
#include <stdbool.h>

bool imu_init(void);                              // 探测 + 配加速度计(幂等);成功 true
bool imu_read_accel(float *x, float *y, float *z); // 读原始加速度(单位 g);失败/无芯片返回 false
bool imu_read_tilt(float *tx, float *ty);          // 屏幕平面倾斜(右为 +tx,下为 +ty),平放≈0
                                                   // 映射在 imu.c 顶部的 MAP_TX/MAP_TY,方向不对改那里
