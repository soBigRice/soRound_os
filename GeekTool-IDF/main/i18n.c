// 多语言表 + CJK fallback 字体。见 i18n.h 顶部说明。
#include "i18n.h"
#include "settings.h"
#include <string.h>

/* 文案表:一列 en 一列 zh,下标 = str_id_t。zh 只用高频字(simsun_16_cjk 千字表内)。 */
static const char *const TXT[S__COUNT][2] = {
    /* settings */
    [S_DISPLAY]     = { "display",      "显示" },
    [S_SOUND]       = { "sound",        "声音" },
    [S_SYSTEM]      = { "system",       "系统" },
    [S_BRIGHTNESS]  = { "brightness",   "亮度" },
    [S_FACE]        = { "face",         "表盘" },
    [S_ALWAYS_ON]   = { "always-on",    "常显" },
    [S_VOLUME]      = { "volume",       "音量" },
    [S_SILENT]      = { "silent",       "静音" },
    [S_ABOUT]       = { "about",        "关于" },
    [S_LANGUAGE]    = { "language",     "语言" },
    [S_ON]          = { "on",           "开" },
    [S_OFF]         = { "off",          "关" },
    [S_AOD_DESC]    = { "screen stays dimmed when idle\ninstead of turning off", "空闲时变暗常显\n而不是自动关屏" },
    [S_SILENT_DESC] = { "mute alarms and beeps",  "关闭提示音和铃声" },
    /* ota */
    [S_OTA_TITLE]   = { "OTA update",   "在线更新" },
    [S_CURRENT]     = { "current",      "当前版本" },
    [S_TAP_UPDATE]  = { "tap update to flash from server", "点更新从云端获取固件" },
    [S_BETA_CH]     = { "beta channel", "测试通道" },
    [S_CHECKING]    = { "checking latest version...", "正在检查最新版本..." },
    [S_UPDATING]    = { "updating - do not power off", "更新中 - 请勿断电" },
    [S_DONE_REBOOT] = { "done - rebooting", "完成 - 正在重启" },
    [S_UPTODATE]    = { "already up to date", "已是最新版本" },
    [S_FAILED]      = { "failed - check url / server", "更新失败 - 检查网络或服务器" },
    [S_CONNECT_WIFI]= { "connect wifi first", "请先连接 WiFi" },
    /* weather */
    [S_WX_CLEAR]    = { "Clear",         "晴" },
    [S_WX_PARTLY]   = { "Partly cloudy", "多云" },
    [S_WX_OVERCAST] = { "Overcast",      "阴" },
    [S_WX_FOG]      = { "Fog",           "雾" },
    [S_WX_DRIZZLE]  = { "Drizzle",       "小雨" },
    [S_WX_RAIN]     = { "Rain",          "雨" },
    [S_WX_SNOW]     = { "Snow",          "雪" },
    [S_WX_SHOWERS]  = { "Rain showers",  "阵雨" },
    [S_WX_SNOW_SHOWERS] = { "Snow showers", "阵雪" },
    [S_WX_THUNDER]  = { "Thunderstorm",  "雷雨" },
    [S_WX_HUM]      = { "hum",           "湿度" },
    [S_WX_LOADING]  = { "loading...",    "加载中..." },
    [S_WX_FAIL]     = { "no wifi / fetch failed", "无网络 / 获取失败" },
};

/* 启动器 app 名(en → zh;不在表里 = 保留英文,如 WiFi/I2C/twin/OTA 专名) */
static const char *const APP_ZH[][2] = {
    { "System",    "系统" },
    { "weather",   "天气" },
    { "calendar",  "日历" },
    { "countdown", "倒计时" },
    { "stopwatch", "秒表" },
    { "settings",  "设置" },
    { "audio",     "音频" },
    { "level",     "水平仪" },
    { "maze",      "迷宫" },
    { "fluid",     "流体" },
    { "dice",      "色子" },
    { "mouse",     "鼠标" },
};

const char *tr(str_id_t id) {
    if (id < 0 || id >= S__COUNT) return "";
    return TXT[id][settings_lang() ? 1 : 0];
}

const char *tr_app_name(const char *en_name) {
    if (!settings_lang()) return en_name;
    for (unsigned i = 0; i < sizeof(APP_ZH) / sizeof(APP_ZH[0]); i++)
        if (strcmp(APP_ZH[i][0], en_name) == 0) return APP_ZH[i][1];
    return en_name;
}

/* 字体:拷一份到 RAM 再挂 fallback(内置字体是 const 在 flash,不能原地改)。
   fallback = 自产精简中文字库 font_cn16(苹方 16px 4bpp,只含 UI 用字,tools/gen_font_cn.swift 生成)——
   LVGL 内置思源 CJK 的字表是 demo 日繁混合集,简体覆盖差(实测缺 显/亮/盘/设 等 40+ UI 字),弃用。 */
LV_FONT_DECLARE(font_cn16);
static lv_font_t f_l, f_m, f_sym;

void i18n_init(void) {
    f_l = lv_font_unscii_16;        f_l.fallback   = &font_cn16;
    f_m = lv_font_unscii_16;        f_m.fallback   = &font_cn16;
    f_sym = lv_font_montserrat_20;  f_sym.fallback = &font_cn16;
}

const lv_font_t *i18n_font_l(void)   { return &f_l; }
const lv_font_t *i18n_font_m(void)   { return &f_m; }
const lv_font_t *i18n_font_sym(void) { return &f_sym; }
