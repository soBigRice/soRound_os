#include <assert.h>
#include <stdio.h>
#include "app.h"
#include "src/display/lv_display_private.h"
extern int64_t host_time_us;
static bool visible = true, key;
static int alarms;
bool launcher_app_visible(void) { return visible; }
void audio_out_init(void) {}
void audio_out_deinit(void) {}
void audio_out_alarm(void) { alarms++; }
void buttons_reset_control(void) { key = false; }
bool buttons_control_pressed(void) { bool pressed = key; key = false; return pressed; }
const lv_font_t *i18n_font_l(void) { return LV_FONT_DEFAULT; }
const lv_font_t *i18n_font_sym(void) { return LV_FONT_DEFAULT; }
#include "../../main/app_countdown.c"

void countdown_regressions(void) {
    lv_obj_t *p = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(p); lv_obj_set_size(p, 466, 466);
    host_time_us = 1000000; countdown_enter(p); begin_run(5);
    host_time_us += 1200000; cd_toggle(); assert(s_state == ST_PAUSE && s_remain_us == 3800000);
    host_time_us += 2000000; countdown_tick(); assert(s_remain_us == 3800000);
    cd_toggle(); assert(s_state == ST_RUN && s_end_us == host_time_us + 3800000);
    lv_display_t *display = lv_display_get_default(); lv_refr_now(display); assert(display->inv_p == 0);
    visible = false; countdown_visibility(false);
    host_time_us += 3900000; countdown_tick();
    assert(s_state == ST_DONE && alarms == 1 && display->inv_p == 0); // 隐藏时到期响铃,无 UI 重绘
    host_time_us += 6100000; countdown_tick(); assert(alarms == 2); // 忙帧只响一次,不补积压
    assert(display->inv_p == 0);
    visible = true; countdown_visibility(true); assert(display->inv_p > 0);
    key = true; countdown_tick(); assert(s_state == ST_IDLE);
    countdown_exit(); lv_obj_delete(p);
    puts("countdown: pause/resume accuracy, hidden alarm, no hidden redraw, bounded reminder catch-up passed");
}
