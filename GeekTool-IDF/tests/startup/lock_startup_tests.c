#include "lvgl.h"
#include "power.h"
#include "buttons.h"
#include "settings.h"
#include <assert.h>
#include <stdio.h>
static bool boot=true,visible,aod,sleeping;
static unsigned poweroffs;
static button_event_t event;
bool identity_boot_active(void){return boot;}
bool launcher_app_visible(void){return false;}
void watchface_init(void){}
void watchface_show(void){visible=true;}
void watchface_hide(void){visible=false;}
void watchface_set_aod(bool value){aod=value;}
void watchface_set_sleep(bool value){sleeping=value;}
void display_sleep(bool value){sleeping=value;}
void display_set_brightness(uint8_t level){(void)level;}
uint8_t settings_brightness(void){return 128;}
uint8_t settings_idle_mode(void){return IDLE_OFF;}
bool power_read(int *soc,pwr_state_t *state){*soc=74;*state=PWR_DISCHARGING;return true;}
void power_off(void){++poweroffs;}
void buttons_init(void){}
void buttons_reset_control(void){}
button_event_t buttons_poll(bool active){assert(!active);button_event_t result=event;event=BUTTON_NONE;return result;}
#include "../../main/lock.c"
int main(void) {
    lv_init();lv_display_t *display=lv_display_create(466,466);assert(display);
    assert(lock_init());lock_set(true);assert(visible && lock_is_locked());
    event=BUTTON_SHORT;button_cb(NULL);assert(visible && lock_is_locked() && event==BUTTON_NONE);
    lv_tick_inc(40000);powersave_cb(NULL);assert(!aod && !sleeping && s_scr==SCR_FULL);
    event=BUTTON_LONG;button_cb(NULL);assert(poweroffs==1);
    boot=false;lv_display_trigger_activity(display);powersave_cb(NULL);assert(s_scr==SCR_FULL);
    event=BUTTON_SHORT;button_cb(NULL);assert(!visible && !lock_is_locked());
    event=BUTTON_SHORT;button_cb(NULL);assert(visible && lock_is_locked());
    lv_tick_inc(13000);powersave_cb(NULL);assert(aod && s_scr==SCR_CALM);
    lv_tick_inc(19000);powersave_cb(NULL);assert(sleeping && s_scr==SCR_OFF);
    lv_deinit();puts("Startup cover blocks short unlock/power save, preserves long power-off and normal lock/AOD/sleep after release");
}
