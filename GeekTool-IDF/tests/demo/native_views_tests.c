// 复用既有原生导出夹具；所有硬件写入口改为失败断言。/ Reuse native fixtures with fail-fast hardware side effects.
#define SOROUND_FIXTURE_ONLY
#define xTaskCreate legacy_fixture_create
#define wifi_service_set_enabled legacy_fixture_wifi
#define ble_twin_start legacy_fixture_ble_start
#define ble_twin_stop legacy_fixture_ble_stop
#define ble_twin_set_rx_cb legacy_fixture_ble_rx
#define audio_out_init legacy_fixture_audio_start
#define audio_out_deinit legacy_fixture_audio_stop
#define audio_out_alarm legacy_fixture_alarm
#include "../../../scripts/site_native/renderer.c"
#undef SOROUND_FIXTURE_ONLY
#undef xTaskCreate
#undef wifi_service_set_enabled
#undef ble_twin_start
#undef ble_twin_stop
#undef ble_twin_set_rx_cb
#undef audio_out_init
#undef audio_out_deinit
#undef audio_out_alarm
#include "demo_mode.h"
int xTaskCreate(void (*task)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle){(void)task;(void)name;(void)stack;(void)arg;(void)priority;(void)handle;assert(!"demo started a worker");return 0;}
void wifi_service_set_enabled(bool value){(void)value;assert(!"demo changed Wi-Fi");}
bool ble_twin_start(void){assert(!"demo started BLE");return false;}
void ble_twin_stop(void){assert(!"demo stopped BLE");}
void ble_twin_set_rx_cb(void (*callback)(const uint8_t *,size_t)){(void)callback;assert(!"demo changed BLE callback");}
void audio_out_init(void){assert(!"demo started audio");}
void audio_out_deinit(void){assert(!"demo stopped audio");}
void audio_out_alarm(void){assert(!"demo sounded alarm");}
int main(int argc,char **argv) {
    lv_init();i18n_init();lv_display_t *display=lv_display_create(466,466);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);lv_display_set_buffers(display,buffer,NULL,sizeof buffer,LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
    lv_display_set_theme(display,lv_theme_default_init(display,lv_color_hex(COL_RED),lv_color_hex(COL_TXT),true,UI_FONT_SYM));
    lv_obj_set_style_bg_color(lv_screen_active(),lv_color_black(),0);tools_test_battery();
    heading=lv_label_create(lv_layer_top());lv_obj_set_style_text_font(heading,UI_FONT_L,0);lv_obj_align(heading,LV_ALIGN_TOP_MID,0,46);
    const demo_page_t *views[]={&demo_calendar,&demo_countdown,&demo_stopwatch,&demo_maze,&demo_fluid,&demo_i2c,&demo_twin};
    const char *names[]={"calendar","countdown","stopwatch","maze","fluid","i2c","twin"};
    for(preview_language=0;preview_language<2;++preview_language)for(unsigned i=0;i<sizeof views/sizeof views[0];++i)for(unsigned variant=0;variant<views[i]->count;++variant) {
        lv_obj_t *root=lv_obj_create(lv_screen_active());lv_obj_remove_style_all(root);lv_obj_set_size(root,466,466);ui_obj_set_scrollable(root,false);
        lv_label_set_text(heading,names[i]);views[i]->enter(root,variant);
        for(unsigned frame=0;frame<60;++frame){host_time_us+=33000;lv_tick_inc(33);if(views[i]->frame)views[i]->frame(frame*33);lv_timer_handler();}
        if(views[i]->visibility){views[i]->visibility(false);lv_tick_inc(50000);host_time_us+=50000000;views[i]->visibility(true);}
        if(argc>1){char name[80];snprintf(name,sizeof name,"demo-%s-%02u-%s",names[i],variant,preview_language?"zh":"en");capture(argv[1],name,display);}
        if(views[i]->exit)views[i]->exit();
        lv_obj_delete(root);lv_timer_handler();
    }
    lv_deinit();puts("demo native tools: all variants EN/ZH, real render/timers/cleanup, no worker/BLE/Wi-Fi/audio writes passed");return 0;
}
