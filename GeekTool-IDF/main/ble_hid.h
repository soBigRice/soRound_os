#pragma once
// BLE HID 鼠标(HOGP)—— NimBLE 外设,host/服务注册共享 ble_core。
// app_mouse 前台时广播为"soRound"鼠标,电脑/手机配对(Just Works 绑定)后收相对位移报文。
#include <stdbool.h>
#include <stdint.h>

bool ble_hid_start(void);                 // 进 app:开广播(host 已起则复用)
void ble_hid_stop(void);                  // 退 app:停广播 + 断连接(host 常驻)
bool ble_hid_connected(void);             // 已连接且加密(可以发报文)
// 发一帧鼠标报文:按键位图 bit0=左 bit1=右 bit2=中,dx/dy 相对位移,wheel 滚轮(上正)
bool ble_hid_mouse(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel);
