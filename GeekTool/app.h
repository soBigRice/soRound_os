#pragma once
// GeekTool —— App 框架的公共接口与主题
#include <Arduino.h>
#include <lvgl.h>
#include "Arduino_GFX_Library.h"
#include "TouchDrvCSTXXX.hpp"

// ---------- 主题色(0xRRGGBB) ----------
#define COL_BG    0x000000
#define COL_RING  0x3ddc84   // 系统电量环
#define COL_WIFI  0x4aa3ff
#define COL_I2C   0xffb454
#define COL_SYS   0xb88cff
#define COL_TXT   0xffffff
#define COL_TXT2  0x8a9099
#define COL_OK    0x3ddc84
#define COL_WARN  0xff6b6b

// ---------- App 接口 ----------
// 每个工具 = 实现 enter/tick/exit + 在 apps[] 注册一行
typedef struct {
  const char *name;                 // 显示名(ASCII)
  uint32_t    color;                // 主题色
  void (*enter)(lv_obj_t *parent);  // 在 parent(整屏)上建 UI
  void (*tick)(void);               // 周期回调(可为 NULL)
  void (*exit)(void);               // 退出清理(可为 NULL)
} app_t;

// 已注册的 app(定义在 GeekTool.ino)
extern const app_t app_wifi;
extern const app_t app_i2c;
extern const app_t app_sys;
extern const app_t *const APPS[];
extern const int APP_COUNT;

// ---------- 平台服务 ----------
void go_home(void);   // 任意 app 内可调用,返回启动器

// ---------- HAL 全局(定义在 GeekTool.ino) ----------
extern Arduino_CO5300 *gfx;
extern TouchDrvCST92xx touch;
extern uint32_t screenW, screenH;
