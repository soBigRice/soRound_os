#pragma once
// BLE 遥控台(HOGP):鼠标 ID1 + 键盘 ID2 + 媒体 ID3,host/服务注册共享 ble_core。
// 设备名和原鼠标报文不变。新增 Report Map 后,旧主机可能需要删除配对后重新发现。
#include <stdbool.h>
#include <stdint.h>

bool ble_hid_start(void);                 // 进 app:开广播(host 已起则复用)
void ble_hid_stop(void);                  // 退 app:停广播 + 断连接(host 常驻)
bool ble_hid_connected(void);             // 已连接且加密;通知是否可用再查 ready
typedef enum { HID_MOUSE, HID_KEYBOARD, HID_MEDIA, HID_REPORT_COUNT } hid_report_t;
// 按所选模式检查通知订阅;配对成功不代表主机已订阅该 Report。
bool ble_hid_ready(hid_report_t report);
// 发一帧鼠标报文:按键位图 bit0=左 bit1=右 bit2=中,dx/dy 相对位移,wheel 滚轮(上正)
bool ble_hid_mouse(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel);
bool ble_hid_key_tap(uint8_t key);         // PageUp/PageDown,有界队列保存完整按下/释放
bool ble_hid_media_tap(uint16_t usage);    // 下列六种 USB Consumer Usage ID
void ble_hid_tick(void);                  // 遥控台每 20ms 调用;遮挡时仍负责释放,不调用 LVGL
void ble_hid_release_all(void);           // 模式切换/遮挡:取消未发送动作,重试释放已发送按键

enum {
    HID_KEY_PAGE_UP = 0x4b, HID_KEY_PAGE_DOWN = 0x4e,
    HID_MEDIA_NEXT = 0xb5, HID_MEDIA_PREVIOUS = 0xb6, HID_MEDIA_PLAY_PAUSE = 0xcd,
    HID_MEDIA_MUTE = 0xe2, HID_MEDIA_VOLUME_UP = 0xe9, HID_MEDIA_VOLUME_DOWN = 0xea
};
