// 锁屏控制:AXP2101 PWRON 侧键 短按=锁/解切换,长按=关机;
// 锁屏后放电超时熄屏、充电常显(M3b)。按键的去抖/长短判定由 AXP2101 硬件完成。
#include "lock.h"
#include "watchface.h"
#include "display.h"
#include "power.h"
#include "settings.h"

#define AOD_MS     12000   // 空闲进入低功耗"平静"态(变暗 + 停止闪烁 + 按分钟刷新)
#define SLEEP_MS   30000   // 仅"自动熄屏"模式:空闲再久则熄屏
#define BR_DIM     0x20    // 平静态亮度

static bool s_locked = false;
typedef enum { SCR_FULL, SCR_CALM, SCR_OFF } scr_t;
static scr_t s_scr = SCR_FULL;

static void set_screen(scr_t s) {
    if (s == s_scr) return;
    s_scr = s;
    switch (s) {
        // FULL=全亮活动态(表盘正常闪/秒点);CALM=变暗 + AOD(停闪、只按分钟刷新);OFF=熄屏
        case SCR_FULL: watchface_set_aod(false); display_sleep(false); display_set_brightness(settings_brightness()); break;
        case SCR_CALM: watchface_set_aod(true);  display_sleep(false); display_set_brightness(BR_DIM);                break;
        case SCR_OFF:  display_set_brightness(0); display_sleep(true);                                                break;
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

/* 省电:仅锁屏时按空闲超时进入平静/熄屏;触摸/按键唤醒。
   空闲即进入"平静"(变暗+停闪+按分钟刷新),无论充放电都生效(插着 USB 也能看到变暗)。
   仅"自动熄屏"模式且在放电时,空闲更久才真正熄屏;充电时当桌面钟常显不熄。 */
static void powersave_cb(lv_timer_t *t) {
    if (!s_locked) { set_screen(SCR_FULL); return; }
    uint32_t idle = lv_display_get_inactive_time(NULL);
    if (idle <= AOD_MS) { set_screen(SCR_FULL); return; }       // 刚操作过 → 全亮活动态

    int soc; pwr_state_t st = PWR_UNKNOWN;
    bool discharging = power_read(&soc, &st) && (st == PWR_DISCHARGING);
    if (idle > SLEEP_MS && settings_idle_mode() == IDLE_OFF && discharging)
        set_screen(SCR_OFF);
    else
        set_screen(SCR_CALM);
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
