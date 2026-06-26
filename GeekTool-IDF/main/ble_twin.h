#pragma once
// 数字孪生 BLE 链路 —— NimBLE GATT 外设(只做 peripheral + broadcaster)。
// 设备向浏览器(Web Bluetooth)notify 传感器帧;浏览器可写回一字节指令做双向交互。
//
// GATT 结构(UUID 与 web/twin.html 必须一致):
//   Service  c0de0001-feed-face-cafe-0123456789ab
//     TX 特征 c0de0002-...  NOTIFY  设备 → 网页(传感器帧)
//     RX 特征 c0de0003-...  WRITE   网页 → 设备(指令,首字节)
// 广播名:GeekTwin
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool ble_twin_start(void);                 // 起协议栈 + 广播(幂等);失败返回 false
void ble_twin_stop(void);                  // 断连 + 停广播 + 释放协议栈(退出 app 必调)
bool ble_twin_connected(void);             // 当前是否有中心(浏览器)连着
bool ble_twin_notify(const uint8_t *data, size_t len);  // 推一帧;未连/未订阅时静默返回 false
void ble_twin_set_rx_cb(void (*cb)(const uint8_t *data, size_t len)); // 设网页指令回调(在 BLE host 任务上下文调用)
