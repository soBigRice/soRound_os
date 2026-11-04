// 验证巡演顺序、控制与退出，服务由不可写夹具替代。/ Test tour/control/cleanup with non-writing fixtures.
#include "demo_mode.h"
#include "settings.h"
#include <assert.h>
#include <stdio.h>
static unsigned entries,exits,frames,last_variant,unsafe_actions;
static lv_indev_data_t pointer={.state=LV_INDEV_STATE_RELEASED};
static void input_read(lv_indev_t *input,lv_indev_data_t *data){(void)input;*data=pointer;}
static void underlying_action(lv_event_t *event){(void)event;++unsafe_actions;}
static const app_t *presented;
static bool covered,physical_control;
void buttons_reset_control(void){physical_control=false;}
bool buttons_control_pressed(void){bool value=physical_control;physical_control=false;return value;}
static void fixture_enter(lv_obj_t *parent,unsigned variant){++entries;last_variant=variant;lv_obj_t *b=lv_button_create(parent);lv_obj_set_pos(b,183,183);lv_obj_set_size(b,100,100);lv_obj_add_event_cb(b,underlying_action,LV_EVENT_CLICKED,NULL);}
static void fixture_frame(uint32_t elapsed){(void)elapsed;++frames;}
static void fixture_exit(void){++exits;}
static void fixture_visibility(bool visible){covered=!visible;}
#define FIXTURE(n) const app_t app_##n={.name=#n}; const demo_page_t demo_##n={.count=2,.enter=fixture_enter,.frame=fixture_frame,.exit=fixture_exit,.visibility=fixture_visibility}
FIXTURE(wifi);FIXTURE(i2c);FIXTURE(sys);FIXTURE(weather);FIXTURE(calendar);FIXTURE(countdown);FIXTURE(stopwatch);FIXTURE(settings);FIXTURE(ota);FIXTURE(audio);FIXTURE(level);FIXTURE(maze);FIXTURE(fluid);FIXTURE(dice);FIXTURE(mouse);FIXTURE(twin);FIXTURE(answers);FIXTURE(zodiac);FIXTURE(merit);FIXTURE(pixels);FIXTURE(garden);FIXTURE(focus);FIXTURE(capsules);FIXTURE(watchfaces);
#undef FIXTURE
uint8_t settings_lang(void){return 1;}
int watchface_count(void){return 62;}
void launcher_demo_header(const app_t *source){presented=source;}
void launcher_demo_chrome(bool visible){(void)visible;}
void launcher_set_title(const char *text){assert(text);}
static unsigned homes;
void go_home(void){++homes;app_demo.exit();}
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *pixels){(void)area;(void)pixels;lv_display_flush_ready(display);}
static void advance(unsigned ms){lv_tick_inc(ms);app_demo.tick();lv_timer_handler();}
int main(void) {
    lv_init();i18n_init();lv_display_t *display=lv_display_create(466,466);assert(display);
    static uint16_t buffer[466*40];lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,buffer,NULL,sizeof buffer,LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
    const unsigned expected=62+23*2;assert(demo_mode_count()==expected);
    lv_obj_t *screen=lv_screen_active();
    for(unsigned repeat=0;repeat<3;++repeat) {
        app_demo.enter(screen);assert(demo_mode_index()==0 && presented==&app_watchfaces);
        unsigned start_entries=entries,start_exits=exits;
        // 第一轮覆盖所有应用，而不是先等62张表盘播放完。/ Visit every app before the extra face variants.
        const app_t *seen[24]={presented};
        for(unsigned i=1;i<24;++i){demo_mode_step(1);advance(33);assert(last_variant==0);for(unsigned j=0;j<i;++j)assert(seen[j]!=presented);seen[i]=presented;}
        physical_control=true;advance(33);assert(demo_mode_paused());unsigned still=frames,index=demo_mode_index();advance(100000);assert(demo_mode_index()==index && frames==still && covered);
        demo_mode_step(1);advance(33);assert(demo_mode_index()==24 && presented==&app_watchfaces && last_variant==1 && covered);
        demo_mode_pause(false);assert(!covered);advance(3500);assert(demo_mode_index()==25);
        app_demo.visibility(false);assert(covered);lv_tick_inc(50000);app_demo.visibility(true);advance(33);assert(demo_mode_index()==25);
        for(unsigned i=26;i<expected;++i){demo_mode_step(1);advance(33);assert(demo_mode_index()==i);}
        demo_mode_step(1);advance(33);assert(demo_mode_index()==0);
        demo_mode_pause(true);demo_mode_step(-1);advance(33);assert(demo_mode_index()==expected-1);
        app_demo.exit();assert(!presented && !demo_mode_paused());assert(entries-start_entries==exits-start_exits-1);
        lv_obj_clean(screen);lv_timer_handler();
    }
    lv_indev_t *input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(input,input_read);
    lv_obj_t *root=lv_obj_create(screen);lv_obj_remove_style_all(root);lv_obj_set_size(root,466,466);app_demo.enter(root);lv_obj_update_layout(screen);
    // 实际触摸先命中输入屏障，再操作暂停/下一项/退出；不能穿透到真实按钮。/ Real pointer input must hit the shield and tour controls only.
    const lv_point_t taps[]={{233,233},{233,336},{325,336},{233,390}};
    for(unsigned i=0;i<4;++i){pointer.point=taps[i];pointer.state=LV_INDEV_STATE_PRESSED;lv_tick_inc(33);lv_indev_read(input);pointer.state=LV_INDEV_STATE_RELEASED;lv_tick_inc(33);lv_indev_read(input);app_demo.tick();lv_timer_handler();if(i==1)assert(demo_mode_paused());if(i==2)assert(demo_mode_index()==1);}
    assert(!unsafe_actions && homes==1 && entries==exits);lv_indev_delete(input);lv_obj_delete(root);
    lv_deinit();puts("demo: all apps/variants, auto/manual wrap, pause, covered time, repeated cleanup passed");return 0;
}
