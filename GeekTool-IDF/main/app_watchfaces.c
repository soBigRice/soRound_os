#include "demo_mode.h"
#include "app.h"
#include "control_ui.h"
#include "watchface_picker.h"
#include "watchface.h"
#include "watchface_ui.h"
#include "watchface_studio_draw.h"
#include "img_store.h"

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

// 拍摄用RAM视图；不触发真实操作。/ RAM-only filming adapter; no real actions.
static lv_obj_t *demo_face;
static unsigned demo_face_index;
static uint32_t demo_face_elapsed,demo_face_second;
static void demo_face_draw(lv_event_t *event) {
    lv_area_t area;lv_obj_get_coords(lv_event_get_target_obj(event),&area);
    watchface_data_t sample={.time={.tm_year=126,.tm_mon=9,.tm_mday=22,.tm_wday=4,.tm_hour=10,.tm_min=8,.tm_sec=(int)(demo_face_elapsed/1000)%60},.battery_valid=true,.battery=74,.wifi=true,.ssid="soRound Studio",.ip="192.168.1.74",.weather_valid=true,.temperature=24,.low=19,.high=27,.code=2,.humidity=62};
    if(demo_face_index<WATCHFACE_LEGACY_COUNT && demo_face_index%WATCHFACE_KIND_COUNT==4)sample.image=img_store_face_image_for(watchface_theme_for((int)demo_face_index));
    watchface_render(lv_event_get_layer(event),&area,&sample,(int)demo_face_index,false);
}
static void demo_watchfaces_enter(lv_obj_t *parent,unsigned variant){demo_face_index=variant;demo_face_elapsed=0;demo_face_second=UINT32_MAX;demo_face=control_surface(parent,0,0,466,466);lv_obj_set_style_bg_color(demo_face,lv_color_black(),0);lv_obj_set_style_bg_opa(demo_face,LV_OPA_COVER,0);lv_obj_add_event_cb(demo_face,demo_face_draw,LV_EVENT_DRAW_MAIN,NULL);}
static void demo_watchfaces_frame(uint32_t elapsed){demo_face_elapsed=elapsed;if(demo_face_second!=elapsed/1000){demo_face_second=elapsed/1000;lv_obj_invalidate(demo_face);}}
static void demo_watchfaces_exit(void){demo_face=NULL;wfs_geometry_release();}
const demo_page_t demo_watchfaces={.count=1,.enter=demo_watchfaces_enter,.frame=demo_watchfaces_frame,.exit=demo_watchfaces_exit};
