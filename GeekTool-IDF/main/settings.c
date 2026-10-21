// 设置状态 + NVS 持久化。亮度直接驱动 CO5300;音量由 audio_out 初始化/滑动时应用到 ES8311。
#include "settings.h"
#include "display.h"
#include "nvs.h"
#include "ui_persistence.h"
#include <string.h>

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

static bool write_settings(const void *snapshot) {
    static const char *const keys[]={"bright","vol","face","idle","silent","beta","lang","dice"};
    static uint8_t committed[8];static bool valid;
    const uint8_t *values=snapshot;bool changed=!valid || memcmp(committed,values,sizeof committed)!=0;
    if(!changed)return true;
    nvs_handle_t h;
    esp_err_t err=nvs_open(NS,NVS_READWRITE,&h);
    if(err==ESP_OK){
        for(unsigned i=0;i<sizeof committed && err==ESP_OK;i++)
            if(!valid || committed[i]!=values[i])err=nvs_set_u8(h,keys[i],values[i]);
        if(err==ESP_OK)err=nvs_commit(h);
        nvs_close(h);
    }
    if(err==ESP_OK){memcpy(committed,values,sizeof committed);valid=true;}
    return err==ESP_OK;
}
void settings_save(void) {
    // 快照先复制再交给低优先级任务；已提交缓存只在全部写入及commit成功后更新。
    // Copy before handing off to the worker; update the durable cache only after every write and commit succeeds.
    uint8_t snapshot[]={s_bright,s_vol,s_face,s_idle,s_silent,s_beta,s_lang,s_dice};
    (void)ui_persistence_save(UI_STORE_SETTINGS,snapshot,sizeof snapshot,write_settings);
}
