#include "app.h"
#include "control_ui.h"
#include "watchface_picker.h"

static watchface_picker_t s_watchfaces;
static void watchfaces_enter(lv_obj_t *parent) {
    launcher_set_title(tr_app_name("Watchfaces"));
    lv_obj_t *panel=control_surface(parent,0,0,466,466);
    watchface_picker_mount(&s_watchfaces,panel);
}
static void watchfaces_tick(void) {watchface_picker_refresh(&s_watchfaces);}
static void watchfaces_exit(void) {watchface_picker_unmount(&s_watchfaces);}
// 沿用设置页每秒预览节拍；App框架在锁屏/遮挡时暂停tick，不添加独立计时器。
// Keep the settings preview's one-second cadence; the app framework pauses covered ticks without a new timer.
const app_t app_watchfaces={.name="Watchfaces",.color=COL_TXT,.enter=watchfaces_enter,
    .tick=watchfaces_tick,.exit=watchfaces_exit,.tick_period_ms=1000};
