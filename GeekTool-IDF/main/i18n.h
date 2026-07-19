#pragma once
// 多语言(en/zh)。语言存 settings(NVS,settings_lang);切换后各页面重进即生效。
// 中文渲染:自产精简字库 font_cn16(苹方 16px,只含 UI 用字,tools/gen_font_cn.swift 生成)挂为现有字体的 fallback ——
// 英文/数字照走 unscii/montserrat,遇到 CJK 自动落到中文字库,现有 app 代码零改动。
// 用词刻意只挑高频字(如"色子"不用"骰子"),避免超出千字表出豆腐块;拿不准的保留英文。
#include "lvgl.h"

typedef enum {
    // settings
    S_DISPLAY, S_SOUND, S_SYSTEM,
    S_BRIGHTNESS, S_FACE, S_ALWAYS_ON, S_VOLUME, S_SILENT, S_ABOUT, S_LANGUAGE,
    S_ON, S_OFF, S_AOD_DESC, S_SILENT_DESC,
    // ota
    S_OTA_TITLE, S_CURRENT, S_TAP_UPDATE, S_BETA_CH,
    S_CHECKING, S_UPDATING, S_DONE_REBOOT, S_UPTODATE, S_FAILED, S_CONNECT_WIFI,
    // weather
    S_WX_CLEAR, S_WX_PARTLY, S_WX_OVERCAST, S_WX_FOG, S_WX_DRIZZLE, S_WX_RAIN,
    S_WX_SNOW, S_WX_SHOWERS, S_WX_SNOW_SHOWERS, S_WX_THUNDER,
    S_WX_HUM, S_WX_LOADING, S_WX_FAIL,
    // stopwatch / countdown
    S_KEY_START, S_RUNNING_LAP, S_PAUSED_RESUME, S_LAP, S_RESET,
    S_CD_PAUSE, S_CD_RESUME, S_CD_DONE, S_CD_TAP_START,
    // dice / fluid / level
    S_DICE_HINT, S_DICE_TAP, S_DICE_DOUBLE, S_NO_SENSOR, S_FLUID_NOIMU,
    // mouse
    S_STARTING, S_CONNECTED, S_MOUSE_PAIR, S_BT_FAIL,
    // dice modes / coin
    S_COIN, S_HEADS, S_TAILS,
    S_DICE_MODE, S_1DIE, S_2DICE, S_3DICE,
    S__COUNT
} str_id_t;

void i18n_init(void);                  // main:settings_init 后、建主题前调(准备 fallback 字体副本)
const char *tr(str_id_t id);           // 取当前语言文案
const char *tr_app_name(const char *en_name);   // 启动器 app 名(查不到返回原名)

// 带 CJK fallback 的字体(RAM 副本;英文全命中不走 fallback,零成本)
const lv_font_t *i18n_font_l(void);    // 正文/标题(unscii_16 基底)
const lv_font_t *i18n_font_m(void);    // 次要文字(同上)
const lv_font_t *i18n_font_sym(void);  // 图标/默认主题(montserrat_20 基底)
