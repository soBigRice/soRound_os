#include "app.h"
#include "tools_ui.h"
#include "font_companion.h"
#include "ui_update.h"
#include "capsule_recorder.h"
#include "settings.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static lv_obj_t *g_main,*g_wave,*g_time,*g_seconds,*g_hint,*g_date,*g_record,*g_list,*g_rows[3],*g_row_names[3],*g_row_times[3],*g_space,*g_empty;
static capsule_snapshot_t s_view;
static bool s_visible=true,s_listing;
static unsigned s_page;
static int s_delete=-1;
static uint8_t s_wave[32];
static void capsules_tick(void);

static lv_obj_t *circle_button(lv_obj_t *parent,int x,int y,int diameter) {
    lv_obj_t *b=tools_surface(parent,x,y,diameter,diameter);ui_obj_set_clickable(b,true);
    lv_obj_set_style_radius(b,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_opa(b,LV_OPA_COVER,0);
    lv_obj_set_style_bg_color(b,lv_color_hex(0x111418),0);lv_obj_set_style_border_width(b,1,0);
    lv_obj_set_style_border_color(b,lv_color_hex(0x40464d),0);return b;
}
static bool recording(void){return s_view.state==CAPSULE_RECORDING || s_view.state==CAPSULE_WAITING;}
static bool busy(void){return recording() || s_view.state==CAPSULE_SAVING || s_view.state==CAPSULE_PLAYING;}
static void wave_draw(lv_event_t *e) {
    lv_layer_t *layer=lv_event_get_layer(e);lv_area_t a;lv_obj_get_coords(g_wave,&a);
    for(unsigned i=0;i<32;++i){int h=4+s_wave[i]*72/255;uint32_t c=recording()&&i>20?0xff4d36:i<2||i>29?TOOLS_GRAY:TOOLS_WHITE;
        tools_line(layer,a.x1+6+(int)i*10,a.y1+40-h/2,a.x1+6+(int)i*10,a.y1+40+h/2,4,c);}
}
static void clock_draw(lv_event_t *e) {
    lv_layer_t *layer=lv_event_get_layer(e);lv_area_t a;lv_obj_get_coords(lv_event_get_target_obj(e),&a);
    tools_dot(layer,a.x1+6,a.y1+7,5,TOOLS_WHITE);tools_dot(layer,a.x1+6,a.y1+25,5,TOOLS_WHITE);
}
static void record_draw(lv_event_t *e) {
    lv_layer_t *layer=lv_event_get_layer(e);lv_area_t a;lv_obj_get_coords(g_record,&a);
    if(recording() || s_view.state==CAPSULE_PLAYING) {
        lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.radius=9;d.bg_color=lv_color_hex(0xff4d36);d.bg_opa=LV_OPA_COVER;
        lv_area_t square={a.x1+32,a.y1+32,a.x1+67,a.y1+67};lv_draw_rect(layer,&d,&square);
    } else tools_dot(layer,a.x1+50,a.y1+50,36,s_view.result==CAPSULE_UNCONFIGURED?TOOLS_GRAY:0xff4d36);
}
static void folder_draw(lv_event_t *e) {
    lv_layer_t *layer=lv_event_get_layer(e);lv_area_t a;lv_obj_get_coords(lv_event_get_target_obj(e),&a);
    tools_line(layer,a.x1+16,a.y1+22,a.x1+25,a.y1+22,2,TOOLS_WHITE);
    tools_line(layer,a.x1+25,a.y1+22,a.x1+29,a.y1+27,2,TOOLS_WHITE);
    tools_line(layer,a.x1+29,a.y1+27,a.x1+43,a.y1+27,2,TOOLS_WHITE);
    tools_line(layer,a.x1+43,a.y1+27,a.x1+43,a.y1+40,2,TOOLS_WHITE);
    tools_line(layer,a.x1+43,a.y1+40,a.x1+16,a.y1+40,2,TOOLS_WHITE);
    tools_line(layer,a.x1+16,a.y1+40,a.x1+16,a.y1+22,2,TOOLS_WHITE);
}
static const char *status_text(void) {
    if(s_view.mic_failed)return tools_text("MIC UNAVAILABLE","麦克风不可用");
    if(s_view.state==CAPSULE_LOADING)return tools_text("LOADING","正在载入");
    if(s_view.state==CAPSULE_WAITING)return tools_text("WAITING FOR AUDIO","等待音频");
    if(s_view.state==CAPSULE_RECORDING)return tools_text("CAPTURING THIS MOMENT","正在记录这一刻");
    if(s_view.state==CAPSULE_SAVING)return tools_text("SAVING","正在保存");
    if(s_view.state==CAPSULE_PLAYING)return tools_text("PLAYING","正在播放");
    if(s_view.muted)return tools_text("SYSTEM MUTE","静音已开启");
    switch(s_view.result) {
        case CAPSULE_UNCONFIGURED:return tools_text("STORAGE NOT CONFIGURED","存储未配置");
        case CAPSULE_CORRUPT_STORAGE:return tools_text("OFFLINE RECOVERY NEEDED","需离线恢复");
        case CAPSULE_FULL:return tools_text("STORAGE FULL","存储已满");
        case CAPSULE_IO_ERROR:return tools_text("FILE I/O FAILED","写入失败");
        case CAPSULE_INVALID:return tools_text("RECORDING UNAVAILABLE","损坏录音");
        default:break;
    }
    if(s_view.saved)return tools_text("SAVED · TAP TO LISTEN","已保存，点击回听");
    if(s_view.cancelled)return tools_text("RECORDING CANCELLED","录音已取消");
    if(s_view.catalog.recovered)return tools_text("INCOMPLETE CLIP REMOVED","未完成录音已清理");
    return tools_text("READY TO RECORD","准备录音");
}
static void record_click(lv_event_t *e) {
    (void)e;if(busy()){capsule_recorder_stop();return;}
    if(s_view.result!=CAPSULE_UNCONFIGURED && s_view.result!=CAPSULE_CORRUPT_STORAGE) {
        s_view.saved=false;(void)capsule_recorder_record();
    }
}
static void show_list(bool show) {
    s_listing=show;s_delete=-1;ui_obj_set_hidden(g_main,show);ui_obj_set_hidden(g_list,!show);capsules_tick();
}
static void folder_click(lv_event_t *e){(void)e;if(!busy())show_list(true);}
static void play_click(lv_event_t *e){(void)e;if(!busy() && s_view.catalog.count && s_view.catalog.entries[0].valid)(void)capsule_recorder_play(s_view.catalog.entries[0].name);}
static void row_event(lv_event_t *e) {
    unsigned i=s_page*3+(unsigned)(uintptr_t)lv_event_get_user_data(e);if(i>=s_view.catalog.count)return;
    if(lv_event_get_code(e)==LV_EVENT_LONG_PRESSED){s_delete=(int)i;capsules_tick();return;}
    if(lv_event_get_code(e)!=LV_EVENT_SHORT_CLICKED)return;
    if(s_delete==(int)i){(void)capsule_recorder_delete(s_view.catalog.entries[i].name);s_delete=-1;}
    else if(s_view.catalog.entries[i].valid){(void)capsule_recorder_play(s_view.catalog.entries[i].name);show_list(false);}
}
static void page_click(lv_event_t *e) {
    int direction=(int)(intptr_t)lv_event_get_user_data(e);unsigned pages=(s_view.catalog.count+2)/3;
    if(direction<0 && s_page)s_page--;
    if(direction>0 && s_page+1<pages)s_page++;
    s_delete=-1;capsules_tick();
}
static void list_refresh(void) {
    ui_obj_set_hidden(g_empty,s_view.catalog.count!=0);
    char text[60];snprintf(text,sizeof text,"%u / 32     %.1f MB",s_view.catalog.count,s_view.catalog.free_bytes/1048576.0);
    ui_text(g_space,text);tools_label_center(g_space,233,63);
    if(s_page*3>=s_view.catalog.count)s_page=0;
    for(unsigned row=0;row<3;++row) {
        unsigned i=s_page*3+row;ui_obj_set_hidden(g_rows[row],i>=s_view.catalog.count);
        if(i>=s_view.catalog.count)continue;
        capsule_entry_t *entry=&s_view.catalog.entries[i];
        struct tm date;localtime_r(&entry->created,&date);
        if(s_delete==(int)i)snprintf(text,sizeof text,"%s",tools_text("DELETE THIS CLIP?","删除这条录音"));
        else if(entry->created>1577836800)strftime(text,sizeof text,"%m.%d   %H:%M",&date);
        else snprintf(text,sizeof text,"CAPSULE %u",i+1);
        ui_text(g_row_names[row],text);tools_label_center(g_row_names[row],134,17);
        if(s_delete==(int)i)snprintf(text,sizeof text,"%s",tools_text("Tap again to confirm","再次点击确认"));
        else if(!entry->valid)snprintf(text,sizeof text,"%s",tools_text("DAMAGED CLIP","损坏录音"));
        else snprintf(text,sizeof text,"%02lu:%02lu  ·  %.0f KB",(unsigned long)(entry->bytes/32000/60),(unsigned long)(entry->bytes/32000%60),entry->bytes/1024.0);
        ui_text(g_row_times[row],text);tools_label_center(g_row_times[row],134,42);
        lv_color_t color=lv_color_hex(s_delete==(int)i?0xff4d36:TOOLS_WHITE);
        if(!lv_color_eq(lv_obj_get_style_text_color(g_row_names[row],0),color))lv_obj_set_style_text_color(g_row_names[row],color,0);
    }
}
static void capsules_tick(void) {
    if(!g_main || !s_visible)return;
    capsule_state_t previous_state=s_view.state;capsule_result_t previous_result=s_view.result;
    bool was_recording=recording();
    capsule_recorder_snapshot(&s_view);
    if(s_listing){list_refresh();return;}
    uint32_t seconds=s_view.samples/CAPSULE_RATE;char text[40];
    snprintf(text,sizeof text,"%02lu",(unsigned long)(seconds/60));
    ui_text(g_time,text);tools_label_center(g_time,185,169);
    snprintf(text,sizeof text,"%02lu",(unsigned long)(seconds%60));
    ui_text(g_seconds,text);tools_label_center(g_seconds,281,169);
    ui_text(g_hint,status_text());tools_label_center(g_hint,233,217);
    // 波形颜色依赖录音状态，按钮依赖状态/错误；静止轮询不重画，真实状态变化不能漏画。
    // Wave colors depend on recording, and the button on state/result; repaint real transitions only.
    if(was_recording!=recording() || memcmp(s_wave,s_view.waveform,sizeof s_wave)){memcpy(s_wave,s_view.waveform,sizeof s_wave);lv_obj_invalidate(g_wave);}
    if(previous_state!=s_view.state || previous_result!=s_view.result)lv_obj_invalidate(g_record);
}
static void capsules_enter(lv_obj_t *parent) {
    launcher_set_title(tools_text("CAPSULES","声音胶囊"));
    s_visible=true;s_listing=false;s_page=0;s_delete=-1;memset(&s_view,0,sizeof s_view);memset(s_wave,0,sizeof s_wave);
    g_main=tools_surface(parent,0,88,466,378);
    tools_label(g_main,"VOICE CAPSULE",&font_companion_20,233,22,TOOLS_GRAY,2);
    lv_obj_t *indicator=tools_surface(g_main,219,46,28,15);lv_obj_set_style_radius(indicator,8,0);
    lv_obj_set_style_border_width(indicator,1,0);lv_obj_set_style_border_color(indicator,lv_color_hex(TOOLS_GRAY),0);
    tools_label(indicator,"I",&font_companion_19,14,7,0xff4d36,0);
    g_wave=tools_surface(g_main,72,66,322,82);lv_obj_add_event_cb(g_wave,wave_draw,LV_EVENT_DRAW_MAIN,NULL);
    g_time=tools_label(g_main,"00",&font_tools_60,185,169,TOOLS_WHITE,0);
    g_seconds=tools_label(g_main,"00",&font_tools_60,281,169,TOOLS_WHITE,0);
    lv_obj_t *clock=tools_surface(g_main,227,159,12,36);lv_obj_add_event_cb(clock,clock_draw,LV_EVENT_DRAW_MAIN,NULL);
    g_hint=tools_label(g_main,"",&font_companion_20,233,217,TOOLS_GRAY,0);
    g_record=circle_button(g_main,183,242,100);lv_obj_add_event_cb(g_record,record_draw,LV_EVENT_DRAW_MAIN,NULL);lv_obj_add_event_cb(g_record,record_click,LV_EVENT_CLICKED,NULL);
    lv_obj_t *folder=circle_button(g_main,105,263,60);lv_obj_add_event_cb(folder,folder_draw,LV_EVENT_DRAW_MAIN,NULL);lv_obj_add_event_cb(folder,folder_click,LV_EVENT_CLICKED,NULL);
    lv_obj_t *play=circle_button(g_main,301,263,60);tools_label(play,LV_SYMBOL_PLAY,UI_FONT_SYM,30,30,TOOLS_WHITE,0);lv_obj_add_event_cb(play,play_click,LV_EVENT_CLICKED,NULL);
    time_t now=time(NULL);struct tm date;localtime_r(&now,&date);char text[32];
    if(now>1577836800)strftime(text,sizeof text,"%m.%d",&date);else snprintf(text,sizeof text,"PCM");
    g_date=tools_label(g_main,text,&font_companion_20,233,352,TOOLS_GRAY,1);
    g_list=tools_surface(parent,0,88,466,378);lv_obj_set_style_bg_color(g_list,lv_color_hex(COL_BG),0);lv_obj_set_style_bg_opa(g_list,LV_OPA_COVER,0);
    tools_label(g_list,tools_text("RECORDINGS","录音列表"),&font_companion_24,233,31,TOOLS_WHITE,1);
    g_space=tools_label(g_list,"",&font_companion_19,233,63,TOOLS_GRAY,0);
    g_empty=tools_label(g_list,tools_text("NO RECORDINGS YET","暂无录音"),&font_companion_22,233,180,TOOLS_GRAY,0);
    for(unsigned i=0;i<3;++i) {
        g_rows[i]=tools_surface(g_list,99,91+(int)i*67,268,59);ui_obj_set_clickable(g_rows[i],true);
        lv_obj_set_style_bg_color(g_rows[i],lv_color_hex(0x111418),0);lv_obj_set_style_bg_opa(g_rows[i],LV_OPA_COVER,0);lv_obj_set_style_radius(g_rows[i],12,0);
        g_row_names[i]=tools_label(g_rows[i],"",&font_companion_20,134,17,TOOLS_WHITE,0);
        g_row_times[i]=tools_label(g_rows[i],"",&font_companion_19,134,42,TOOLS_GRAY,0);
        lv_obj_add_event_cb(g_rows[i],row_event,LV_EVENT_ALL,(void *)(uintptr_t)i);
    }
    lv_obj_t *prev=circle_button(g_list,174,323,36);tools_label(prev,"<",&font_companion_22,18,18,TOOLS_WHITE,0);lv_obj_add_event_cb(prev,page_click,LV_EVENT_CLICKED,(void *)(intptr_t)-1);
    lv_obj_t *next=circle_button(g_list,256,323,36);tools_label(next,">",&font_companion_22,18,18,TOOLS_WHITE,0);lv_obj_add_event_cb(next,page_click,LV_EVENT_CLICKED,(void *)(intptr_t)1);
    tools_label(g_list,tools_text("HOLD TO DELETE","长按删除"),&font_companion_19,233,303,TOOLS_GRAY,0);
    ui_obj_set_hidden(g_list,true);capsule_recorder_enter();capsules_tick();
}
static bool capsules_back(void){if(s_listing){show_list(false);return true;}return false;}
static void capsules_visibility(bool visible){s_visible=visible;if(!visible && busy())(void)capsule_recorder_stop();}
static void capsules_exit(void){capsule_recorder_exit();g_main=g_wave=g_time=g_seconds=g_hint=g_date=g_record=g_list=g_space=g_empty=NULL;}
const app_t app_capsules={.name="Capsules",.color=COL_TXT,.enter=capsules_enter,.tick=capsules_tick,.exit=capsules_exit,
    .back=capsules_back,.visibility=capsules_visibility,.tick_period_ms=50};
