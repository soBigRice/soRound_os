#pragma once
// NimBLE 共享核心:host 只 init 一次,首次启动即注册【全部】模块的 GATT 服务
// (NimBLE 不支持 host 起来后再加服务 —— twin 和 HID 鼠标各自注册会看先来后到,故收口到这里)。
// 各模块(ble_twin / ble_hid)只管自己的广播 + GAP 事件;同一时间只有一个前台 app 在广播。
#include <stdbool.h>
#include <stdint.h>

// 幂等。sync_cb:host 同步完成后回调(带本机地址类型,可直接开广播);
// 若已同步则立即回调。后调用者覆盖前者(前台 app 独占 BLE)。
bool ble_core_start(void (*sync_cb)(uint8_t addr_type));
