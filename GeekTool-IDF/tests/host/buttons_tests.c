#include <assert.h>
#include <stdio.h>
#include "app.h"
#include "buttons.h"
#include "driver/gpio.h"
#include "power.h"
#include "settings.h"

int64_t host_time_us;
static int level = 1, power_event, shutdowns, power_reads;
static bool active = true, covered;
bool identity_boot_active(void){return false;} // Normal post-boot mapping; startup guard has its own test.
int gpio_config(const gpio_config_t *config) {
    assert(config->pin_bit_mask == 1 && config->mode == GPIO_MODE_INPUT);
    return 0;
}
int gpio_get_level(int pin) { assert(pin == 0); return level; }
void power_key_init(void) { power_event = 0; }
int power_key_event(void) { power_reads++; int event=power_event; power_event=0; return event; }
void power_off(void) { shutdowns++; }
bool power_read(int *soc, pwr_state_t *state) { (void)soc; (void)state; return false; }
uint8_t settings_brightness(void) { return 128; }
uint8_t settings_idle_mode(void) { return IDLE_AOD; }
void watchface_init(void) {}
void watchface_show(void) {}
void watchface_hide(void) {}
void watchface_set_sleep(bool sleep) { (void)sleep; }
void watchface_set_aod(bool aod) { (void)aod; }
void display_sleep(bool sleep) { (void)sleep; }
void display_set_brightness(uint8_t brightness) { (void)brightness; }
const lv_font_t *i18n_font_l(void) { return LV_FONT_DEFAULT; }
const lv_font_t *i18n_font_m(void) { return LV_FONT_DEFAULT; }
const lv_font_t *i18n_font_sym(void) { return LV_FONT_DEFAULT; }
const char *tr(str_id_t id) { (void)id; return "test"; }
#include "../../main/lock.c"
#include "../../main/app_stopwatch.c"
bool launcher_app_visible(void) { return active && !covered && !lock_is_locked(); }

static void poll(int64_t elapsed) { host_time_us+=elapsed; button_cb(NULL); }
static void press_boot(void) { level=0; poll(20000); poll(40000); }
static void release_boot(void) { level=1; poll(20000); poll(40000); }
static void reset(void) {
    level=1; covered=false; active=true; power_event=shutdowns=power_reads=0;
    host_time_us+=3000000; buttons_init(); lock_set(false);
}
int main(void) {
    lv_init(); lv_display_create(466,466);
    reset();
    press_boot(); assert(!lock_is_locked());
    release_boot(); assert(lock_is_locked() && shutdowns==0);
    press_boot(); release_boot(); assert(!lock_is_locked());

    reset(); press_boot(); poll(2000000);
    assert(shutdowns==1 && !lock_is_locked());
    poll(500000); assert(shutdowns==1);
    release_boot(); assert(!lock_is_locked() && shutdowns==1);
    press_boot(); release_boot(); assert(lock_is_locked());

    reset(); // GPIO 抖动不产生短按;上电时按住必须先释放再开始检测。
    level=0; poll(10000); level=1; poll(10000); poll(50000); assert(!lock_is_locked());
    level=0; buttons_init(); poll(3000000); assert(shutdowns==0);
    release_boot(); assert(!lock_is_locked());
    press_boot(); release_boot(); assert(lock_is_locked());

    reset(); press_boot(); level=1; poll(2500000); poll(40000);
    assert(shutdowns==1 && !lock_is_locked()); // 忙帧跨过长按阈值仍不能算短按。

    reset(); power_event=1; poll(100000);
    assert(!lock_is_locked() && buttons_control_pressed() && !buttons_control_pressed());
    power_event=2; poll(100000); assert(shutdowns==0 && !buttons_control_pressed());
    covered=true; power_event=1; poll(100000); covered=false;
    assert(!buttons_control_pressed());
    power_event=1; poll(100000); buttons_reset_control(); assert(!buttons_control_pressed());
    power_event=1; poll(100000); host_time_us+=300000; assert(!buttons_control_pressed());
    lock_set(true); power_event=1; poll(100000); assert(lock_is_locked() && !buttons_control_pressed());

    reset(); power_reads=0; for(int i=0;i<10;i++) poll(20000); assert(power_reads==2);
    lv_obj_t *page=lv_obj_create(lv_screen_active()); stopwatch_enter(page);
    power_event=1; poll(100000); stopwatch_tick(); assert(s_run && !lock_is_locked());
    host_time_us+=500000; power_event=1; poll(100000); stopwatch_tick(); assert(!s_run && s_base_us==600000);
    power_event=1; poll(100000); stopwatch_tick(); assert(s_run);
    press_boot(); release_boot(); assert(lock_is_locked() && s_run);
    assert(!buttons_control_pressed());
    stopwatch_exit(); lv_obj_delete(page);
    puts("physical buttons: mapping, debounce, long/short exclusion, hidden input, expiry, I2C cadence and real stopwatch passed");
    return 0;
}
