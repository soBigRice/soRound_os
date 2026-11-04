#include "demo_mode.h"
#include "font_companion.h"
#include "settings.h"
#include "buttons.h"
#include "watchface.h"
#include "lvgl_compat.h"
#include <stdio.h>

typedef struct { const app_t *app; const demo_page_t *page; } chapter_t;
// 跟随真实注册表逐项列出；测试校验覆盖，表盘数量在运行时读取。/ Cover the registry; resolve the face count at runtime.
static const chapter_t chapters[] = {
    {&app_watchfaces,&demo_watchfaces}, {&app_wifi,&demo_wifi}, {&app_i2c,&demo_i2c},
    {&app_sys,&demo_sys}, {&app_weather,&demo_weather}, {&app_calendar,&demo_calendar},
    {&app_countdown,&demo_countdown}, {&app_stopwatch,&demo_stopwatch}, {&app_settings,&demo_settings},
    {&app_ota,&demo_ota}, {&app_audio,&demo_audio}, {&app_level,&demo_level},
    {&app_maze,&demo_maze}, {&app_fluid,&demo_fluid}, {&app_dice,&demo_dice},
    {&app_mouse,&demo_mouse}, {&app_twin,&demo_twin}, {&app_answers,&demo_answers},
    {&app_zodiac,&demo_zodiac}, {&app_merit,&demo_merit}, {&app_pixels,&demo_pixels},
    {&app_garden,&demo_garden}, {&app_focus,&demo_focus}, {&app_capsules,&demo_capsules}
};
enum { HOLD_MS=6500, FACE_MS=3500, PANEL_MS=3000 };
static lv_obj_t *root, *page_root, *shield, *panel, *badge, *pause_label;
static const chapter_t *current;
static unsigned index_now;
static uint32_t elapsed, last_tick, panel_at;
static bool paused;
static int pending_step;
static bool pending_exit;
static unsigned count_for(const chapter_t *chapter) {
    return chapter->app==&app_watchfaces?(unsigned)watchface_count():chapter->page->count;
}
unsigned demo_mode_count(void) {
    unsigned total=0;
    for(unsigned i=0;i<sizeof chapters/sizeof chapters[0];++i)total+=count_for(&chapters[i]);
    return total;
}
unsigned demo_mode_index(void){return index_now;}
bool demo_mode_paused(void){return paused;}
static void close_page(void) {
    if(current && current->page->exit)current->page->exit();
    current=NULL;
    if(page_root){lv_obj_delete(page_root);page_root=NULL;}
}
static void show_page(void) {
    close_page();
    // 第一轮先展示每个应用，之后展开全部子功能和剩余表盘。/ Show every app before expanding its remaining variants.
    unsigned chapter_count=sizeof chapters/sizeof chapters[0],variant=0;
    if(index_now<chapter_count)current=&chapters[index_now];
    else {
        unsigned remaining=index_now-chapter_count;
        for(unsigned i=0;i<chapter_count;++i) {
            unsigned extras=count_for(&chapters[i])-1;
            if(remaining<extras){current=&chapters[i];variant=remaining+1;break;}
            remaining-=extras;
        }
    }
    if(!current)return;
    launcher_demo_header(current->app);
    launcher_set_title(tr_app_name(current->app->name));
    page_root=lv_obj_create(root);lv_obj_remove_style_all(page_root);
    lv_obj_set_size(page_root,466,466);ui_obj_set_scrollable(page_root,false);
    current->page->enter(page_root,variant);
    if(current->page->fullscreen && current->page->fullscreen(variant))launcher_demo_chrome(false);
    // 透明输入层阻止真实控件收到拍摄点击，所有控制只作用于演示会话。/ The shield keeps filming input out of real controls.
    lv_obj_move_to_index(shield,-1);lv_obj_move_to_index(panel,-1);lv_obj_move_to_index(badge,-1);
    elapsed=0;last_tick=lv_tick_get();
    if(current->page->frame)current->page->frame(0);
    if(paused && current->page->visibility)current->page->visibility(false);
    char text[40];snprintf(text,sizeof text,"DEMO  %03u / %03u",index_now+1,demo_mode_count());
    lv_label_set_text(lv_obj_get_child(badge,0),text);
}
void demo_mode_step(int direction) {
    if(root && direction)pending_step=direction<0?-1:1;
}
void demo_mode_pause(bool value) {
    if(!root || paused==value)return;
    paused=value;last_tick=lv_tick_get();
    if(current && current->page->visibility)current->page->visibility(!paused);
    lv_label_set_text(pause_label,paused?LV_SYMBOL_PLAY:LV_SYMBOL_PAUSE);
    lv_display_trigger_activity(NULL);
}
static void panel_show(bool visible) {
    ui_obj_set_hidden(panel,!visible);panel_at=lv_tick_get();
}
static void toggle_panel(lv_event_t *event) {
    (void)event;panel_show(lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN));
}
static void gesture(lv_event_t *event) {
    (void)event;lv_indev_t *input=lv_indev_active();if(!input)return;
    lv_dir_t direction=lv_indev_get_gesture_dir(input);
    if(direction==LV_DIR_LEFT)demo_mode_step(1);
    else if(direction==LV_DIR_RIGHT)demo_mode_step(-1);
}
static void control(lv_event_t *event) {
    intptr_t action=(intptr_t)lv_event_get_user_data(event);panel_at=lv_tick_get();
    if(action==0)demo_mode_pause(!paused);
    else if(action==2)pending_exit=true;
    else demo_mode_step((int)action);
}
static lv_obj_t *button(lv_obj_t *parent,int x,int y,int width,const char *text,lv_event_cb_t callback,intptr_t action) {
    lv_obj_t *b=lv_button_create(parent);lv_obj_remove_style_all(b);
    lv_obj_set_pos(b,x,y);lv_obj_set_size(b,width,44);ui_obj_set_scrollable(b,false);
    ui_obj_set_gesture_bubble(b,false);ui_obj_set_event_bubble(b,false);
    lv_obj_set_style_bg_color(b,lv_color_hex(0x202328),0);lv_obj_set_style_bg_opa(b,LV_OPA_COVER,0);
    lv_obj_set_style_radius(b,22,0);lv_obj_add_event_cb(b,callback,LV_EVENT_CLICKED,(void *)action);
    lv_obj_t *label=lv_label_create(b);lv_obj_set_style_text_font(label,UI_FONT_SYM,0);
    lv_obj_set_style_text_color(label,lv_color_white(),0);lv_label_set_text(label,text);lv_obj_center(label);return b;
}
static void demo_enter(lv_obj_t *parent) {
    root=parent;paused=false;pending_step=0;pending_exit=false;index_now=0;buttons_reset_control();
    shield=lv_obj_create(root);lv_obj_remove_style_all(shield);lv_obj_set_size(shield,466,466);
    ui_obj_set_clickable(shield,true);ui_obj_set_scrollable(shield,false);
    ui_obj_set_gesture_bubble(shield,false);ui_obj_set_event_bubble(shield,false);
    lv_obj_add_event_cb(shield,toggle_panel,LV_EVENT_CLICKED,NULL);
    lv_obj_add_event_cb(shield,gesture,LV_EVENT_GESTURE,NULL);
    panel=lv_obj_create(root);lv_obj_remove_style_all(panel);lv_obj_set_pos(panel,86,302);lv_obj_set_size(panel,294,120);
    lv_obj_set_style_bg_color(panel,lv_color_hex(0x080a0d),0);lv_obj_set_style_bg_opa(panel,245,0);
    lv_obj_set_style_radius(panel,24,0);ui_obj_set_scrollable(panel,false);
    button(panel,18,12,74,LV_SYMBOL_PREV,control,-1);
    lv_obj_t *b=button(panel,110,12,74,LV_SYMBOL_PAUSE,control,0);pause_label=lv_obj_get_child(b,0);
    button(panel,202,12,74,LV_SYMBOL_NEXT,control,1);
    b=button(panel,69,66,156,LV_SYMBOL_CLOSE,control,2);
    lv_obj_center(lv_obj_get_child(b,0));
    badge=button(root,135,426,196,"",toggle_panel,0);lv_obj_set_height(badge,26);
    lv_obj_t *label=lv_obj_get_child(badge,0);lv_obj_set_style_text_font(label,&font_companion_17,0);
    lv_obj_set_style_text_color(label,lv_color_hex(0xb7bdc6),0);lv_obj_center(label);
    panel_show(false);show_page();
}
static void demo_tick(void) {
    if(!root)return;
    lv_display_trigger_activity(NULL);
    if(buttons_control_pressed())demo_mode_pause(!paused);
    if(pending_exit){pending_exit=false;go_home();return;}
    uint32_t now=lv_tick_get(),delta=now-last_tick;last_tick=now;
    if(!lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN) && now-panel_at>=PANEL_MS)panel_show(false);
    if(!paused)elapsed+=delta;
    uint32_t duration=current && current->app==&app_watchfaces?FACE_MS:HOLD_MS;
    if(pending_step || (!paused && elapsed>=duration && lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN))) {
        int direction=pending_step?pending_step:1;pending_step=0;
        unsigned count=demo_mode_count();index_now=(index_now+count+direction)%count;show_page();return;
    }
    if(!paused && current && current->page->frame)current->page->frame(elapsed);
}
static void demo_visibility(bool visible) {
    last_tick=lv_tick_get();
    if(current && current->page->visibility)current->page->visibility(visible && !paused);
}
static void demo_exit(void) {
    close_page();root=shield=panel=badge=pause_label=NULL;paused=false;pending_step=0;pending_exit=false;
    buttons_reset_control();launcher_demo_header(NULL);
}
const app_t app_demo={.name="Demo",.color=COL_TXT,.enter=demo_enter,.tick=demo_tick,.exit=demo_exit,
    .tick_period_ms=33,.visibility=demo_visibility};
