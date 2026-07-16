#pragma once
// GeekTool-IDF App 框架(LVGL 9)
#include "lvgl.h"

// ---- Nothing 单色主题:黑底 / 白字 / 灰次 / 一个红强调 ----
#define COL_BG    0x000000   // 黑(背景)
#define COL_TXT   0xffffff   // 白(主文字/线条)
#define COL_TXT2  0x6a6a6e   // 灰(次要文字)
#define COL_RED   0xd1283a   // 唯一强调色(低电量/危险/选中)
#define COL_CHARGE 0x30d158  // 充电态功能绿:电量环 + ⚡ 仅插电时变绿(柔和不刺眼,黑白红基调外的唯一功能色)

// 旧的多彩别名统一收敛到黑白红(改这里 = 全局变色,各 app 不用动)
#define COL_RING  COL_TXT
#define COL_WIFI  COL_TXT
#define COL_I2C   COL_TXT
#define COL_SYS   COL_TXT
#define COL_OTA   COL_TXT
#define COL_OK    COL_TXT
#define COL_WARN  COL_RED

// ---- 字体(集中管理:将来换 Nothing Ndot 点阵字只改这三行)----
// 正文用内置点阵像素字 unscii;含图标(⚡ / ‹ › / 键盘符号)的标签用带符号的 montserrat
#define UI_FONT_L    &lv_font_unscii_16        // 正文 / 标题
#define UI_FONT_M    &lv_font_unscii_16        // 次要文字
#define UI_FONT_SYM  &lv_font_montserrat_20    // 图标标签 + 默认主题字体

// App 接口:每个工具实现 enter/tick/exit,并在 APPS[] 注册
typedef struct {
    const char *name;                 // 显示名(ASCII)
    uint32_t    color;                // 主题色(单色化后基本都是白)
    void (*enter)(lv_obj_t *parent);  // 在 parent(整屏)建 UI
    void (*tick)(void);               // 周期回调(可 NULL),由 lv_timer 调度
    void (*exit)(void);               // 退出清理(可 NULL)
    bool (*back)(void);               // 返回键/右滑时先调(可 NULL):返 true=已消费(退子页),false=退出 app
} app_t;

extern const app_t app_wifi;
extern const app_t app_i2c;
extern const app_t app_sys;
extern const app_t app_weather;
extern const app_t app_calendar;
extern const app_t app_countdown;
extern const app_t app_settings;   // 含 about(徽标/版本/设备),往下滑可见
extern const app_t app_ota;
extern const app_t app_audio;
extern const app_t app_level;
extern const app_t app_maze;
extern const app_t app_stopwatch;
extern const app_t app_fluid;
extern const app_t app_dice;
extern const app_t app_twin;
extern const app_t *const APPS[];
extern const int APP_COUNT;

void wifi_service_start(void);   // 开机自动起 WiFi 并重连记住的 AP(在 app_wifi.c,main 启动末尾调用)
void wifi_service_set_enabled(bool on);   // 快捷面板:开关 WiFi(开=连记住的 AP,关=断开省电)
bool wifi_service_enabled(void);          // 当前 WiFi 开关状态

// 天气数据(app_weather.c 持有 + 拉取),供天气表盘共用
void weather_poll(void);                                                   // 后台按需拉取
bool weather_cached(int *temp, int *lo, int *hi, int *code, int *hum);     // 取缓存,有数据返回 true

void launcher_start(void);   // 创建启动器并加载(需在 lvgl_port 锁内调用)
void go_home(void);          // app 内返回启动器
void launcher_set_title(const char *t);   // app 可在 enter 里改顶部标题(如天气→城市)
