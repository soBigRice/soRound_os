#pragma once
#include <stdbool.h>
#include <stdint.h>
// HID GAP 生命周期转发；不创建第二个协议栈或更改 HID 报告。
// Forward HID GAP lifetime without a second host or altered HID reports.
void ble_media_hid_link(uint16_t connection, bool encrypted);
