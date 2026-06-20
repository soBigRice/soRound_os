#pragma once
// GeekTool-IDF App 框架(LVGL 9)
#include "lvgl.h"

// 主题色
#define COL_BG    0x000000
#define COL_RING  0x3ddc84
#define COL_WIFI  0x4aa3ff
#define COL_I2C   0xffb454
#define COL_SYS   0xb88cff
#define COL_OTA   0x2ee6c5
#define COL_TXT   0xffffff
#define COL_TXT2  0x8a9099
#define COL_OK    0x3ddc84
#define COL_WARN  0xff6b6b

// App 接口:每个工具实现 enter/tick/exit,并在 APPS[] 注册
typedef struct {
    const char *name;                 // 显示名(ASCII)
    uint32_t    color;                // 主题色
    void (*enter)(lv_obj_t *parent);  // 在 parent(整屏)建 UI
    void (*tick)(void);               // 周期回调(可 NULL),由 lv_timer 调度
    void (*exit)(void);               // 退出清理(可 NULL)
} app_t;

extern const app_t app_wifi;
extern const app_t app_i2c;
extern const app_t app_sys;
extern const app_t app_ota;
extern const app_t *const APPS[];
extern const int APP_COUNT;

void launcher_start(void);   // 创建启动器并加载(需在 lvgl_port 锁内调用)
void go_home(void);          // app 内返回启动器
