// 设置状态 + NVS 持久化。亮度直接驱动 CO5300;音量暂时只存值(GeekTool 还没接音频通路)。
#include "settings.h"
#include "display.h"
#include "nvs.h"

#define NS "settings"

static uint8_t s_bright = 0xC0;        // 默认 ~75%
static uint8_t s_vol    = 60;          // 默认 60%
static uint8_t s_face   = 0;           // 默认第 0 款表盘
static uint8_t s_idle   = IDLE_AOD;    // 默认低功耗长显
static uint8_t s_silent = 0;           // 默认不静音
static uint8_t s_beta   = 0;           // 默认只收正式版 OTA(0=stable,1=beta 通道)
static uint8_t s_lang   = 0;           // 界面语言:0=English(默认),1=中文
static uint8_t s_dice   = 1;           // 骰子模式:0/1/2=1/2/3 颗骰子,3=硬币(默认 2 颗)

void settings_init(void) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, "bright", &s_bright);
        nvs_get_u8(h, "vol", &s_vol);
        nvs_get_u8(h, "face", &s_face);
        nvs_get_u8(h, "idle", &s_idle);
        nvs_get_u8(h, "silent", &s_silent);
        nvs_get_u8(h, "beta", &s_beta);
        nvs_get_u8(h, "lang", &s_lang);
        nvs_get_u8(h, "dice", &s_dice);
        nvs_close(h);
    }
    if (s_bright < SETTINGS_BRIGHT_MIN) s_bright = SETTINGS_BRIGHT_MIN;   // 夹住开机亮度,防黑屏
    if (s_idle > IDLE_OFF) s_idle = IDLE_AOD;
    display_set_brightness(s_bright);
}

uint8_t settings_brightness(void) { return s_bright; }
void settings_set_brightness(uint8_t v) {
    if (v < SETTINGS_BRIGHT_MIN) v = SETTINGS_BRIGHT_MIN;   // 别拖到看着像黑屏
    s_bright = v;
    display_set_brightness(v);
}

uint8_t settings_volume(void) { return s_vol; }
void settings_set_volume(uint8_t v) { if (v > 100) v = 100; s_vol = v; }

uint8_t settings_face(void) { return s_face; }
void settings_set_face(uint8_t v) { s_face = v; }

uint8_t settings_idle_mode(void) { return s_idle; }
void settings_set_idle_mode(uint8_t v) { s_idle = (v > IDLE_OFF) ? IDLE_AOD : v; }

uint8_t settings_silent(void) { return s_silent; }
void settings_set_silent(uint8_t v) { s_silent = v ? 1 : 0; }

uint8_t settings_beta(void) { return s_beta; }
void settings_set_beta(uint8_t v) { s_beta = v ? 1 : 0; }

uint8_t settings_lang(void) { return s_lang; }
void settings_set_lang(uint8_t v) { s_lang = v ? 1 : 0; }

uint8_t settings_dice(void) { return s_dice > 3 ? 1 : s_dice; }
void settings_set_dice(uint8_t v) { s_dice = v > 3 ? 1 : v; }

void settings_save(void) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "bright", s_bright);
        nvs_set_u8(h, "vol", s_vol);
        nvs_set_u8(h, "face", s_face);
        nvs_set_u8(h, "idle", s_idle);
        nvs_set_u8(h, "silent", s_silent);
        nvs_set_u8(h, "beta", s_beta);
        nvs_set_u8(h, "lang", s_lang);
        nvs_set_u8(h, "dice", s_dice);
        nvs_commit(h);
        nvs_close(h);
    }
}
