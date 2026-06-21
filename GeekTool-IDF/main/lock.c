// 锁屏控制:AXP2101 PWRON 侧键 短按=锁/解切换,长按=关机;
// 锁屏后放电超时熄屏、充电常显(M3b)。按键的去抖/长短判定由 AXP2101 硬件完成。
#include "lock.h"
#include "watchface.h"
#include "display.h"
#include "power.h"
#include "settings.h"

#define DIM_MS     15000   // 空闲变暗
#define SLEEP_MS   30000   // 空闲熄屏
#define BR_FULL    0xFF
#define BR_DIM     0x20

static bool s_locked = false;
typedef enum { SCR_FULL, SCR_DIM, SCR_OFF } scr_t;
static scr_t s_scr = SCR_FULL;

static void set_screen(scr_t s) {
    if (s == s_scr) return;
    s_scr = s;
    switch (s) {
        case SCR_FULL: display_sleep(false); display_set_brightness(settings_brightness()); break;
        case SCR_DIM:  display_sleep(false); display_set_brightness(BR_DIM);  break;
        case SCR_OFF:  display_set_brightness(0); display_sleep(true);         break;
    }
}

void lock_set(bool locked) {
    s_locked = locked;
    set_screen(SCR_FULL);
    lv_display_trigger_activity(NULL);    // 重置空闲计时
    if (locked) watchface_show();
    else        watchface_hide();
}

bool lock_is_locked(void) { return s_locked; }

static void unlock_gesture(lv_event_t *e) {
    if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_TOP) lock_set(false);   // 上滑解锁
}

/* 省电:仅锁屏 + 放电时按空闲超时变暗/熄屏;充电常显;触摸/按键唤醒 */
static void powersave_cb(lv_timer_t *t) {
    if (!s_locked) { set_screen(SCR_FULL); return; }
    int soc; pwr_state_t st = PWR_UNKNOWN;
    bool ok = power_read(&soc, &st);
    if (ok && (st == PWR_CHARGING || st == PWR_FULL)) { set_screen(SCR_FULL); return; }
    uint32_t idle = lv_display_get_inactive_time(NULL);
    if      (idle > SLEEP_MS) set_screen(SCR_OFF);
    else if (idle > DIM_MS)   set_screen(SCR_DIM);
    else                      set_screen(SCR_FULL);
}

/* PWRON 侧键:短按切换锁/解,长按关机(硬件已做去抖+长短判定) */
static void button_cb(lv_timer_t *t) {
    int ev = power_key_event();
    if (ev == 1) { lock_set(!s_locked); lv_display_trigger_activity(NULL); }   // 短按
    else if (ev == 2) power_off();                                             // 长按
}

void lock_init(void) {
    watchface_init();
    lv_obj_add_event_cb(watchface_root(), unlock_gesture, LV_EVENT_GESTURE, NULL);

    power_key_init();                            // 使能 PWRON 键 IRQ
    lv_timer_create(button_cb, 100, NULL);       // 100ms 轮询 PWRON IRQ
    lv_timer_create(powersave_cb, 300, NULL);    // 300ms:触摸唤醒延迟更短
}
