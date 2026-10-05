// Direct category navigation. Returning from a detail goes to Settings, not the launcher.
#include "control_ui.h"
#include "settings.h"
#include "watchface.h"
#include "watchface_ui.h"
#include "glyph.h"
#include "audio_out.h"
#include "esp_app_desc.h"
#include "img_store.h"
#include <stdio.h>

enum { SETTINGS_HOME, SETTINGS_DISPLAY, SETTINGS_FACE, SETTINGS_SOUND, SETTINGS_LANG, SETTINGS_ABOUT };
static lv_obj_t *s_parent,*s_panel,*s_value,*s_slider,*s_aod,*s_mute,*s_face_preview;
static int s_page;
static bool s_audio_ready;
static const char *word(const char *zh,const char *en) {return settings_lang()?zh:en;}
static void rebuild(void *arg);
static void queue_rebuild(void) {lv_async_call_cancel(rebuild,NULL);lv_async_call(rebuild,NULL);}
static lv_obj_t *center_text(const char *value,int y,int width,uint32_t color,const lv_font_t *font) {
    lv_obj_t *l=control_label(s_panel,value,font,(466-width)/2,y,width,color);
    lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);return l;
}
static void open_page(lv_event_t *e) {s_page=(int)(intptr_t)lv_event_get_user_data(e);queue_rebuild();}
static void menu_row(int page,const char *name,const char *detail,int y,int width) {
    lv_obj_t *row=control_button(s_panel,(466-width)/2,y,width,64,open_page,(void *)(intptr_t)page);
    control_label(row,name,&font_location_24,20,5,width-66,CONTROL_WHITE);
    control_label(row,detail,control_small_font(),20,36,width-66,CONTROL_GRAY);
    control_label(row,LV_SYMBOL_RIGHT,UI_FONT_SYM,width-36,20,20,CONTROL_GRAY);
}
static void update_number(void) {
    char b[12];int value=s_page==SETTINGS_DISPLAY?settings_brightness()*100/255:settings_volume();
    snprintf(b,sizeof b,"%d%%",value);lv_label_set_text(s_value,b);
}
static void slider_changed(lv_event_t *e) {
    int v=lv_slider_get_value(lv_event_get_target_obj(e));
    if(s_page==SETTINGS_DISPLAY)settings_set_brightness((uint8_t)v);
    else {settings_set_volume((uint8_t)v);audio_out_set_volume((uint8_t)v);}
    update_number();
}
static void save(lv_event_t *e) {(void)e;settings_save();}
static void blip(lv_event_t *e) {(void)e;audio_out_blip();}
static void toggle(lv_event_t *e) {
    bool on=lv_obj_has_state(lv_event_get_target_obj(e),LV_STATE_CHECKED);
    if(s_page==SETTINGS_DISPLAY)settings_set_idle_mode(on?IDLE_AOD:IDLE_OFF);
    else settings_set_silent(on);
    settings_save();queue_rebuild();
}
static void pick_face(lv_event_t *e) {
    int count=watchface_count(),index=(watchface_selected()+(int)(intptr_t)lv_event_get_user_data(e)+count)%count;
    watchface_select(index);settings_set_face((uint8_t)index);settings_save();queue_rebuild();
}
static void pick_theme(lv_event_t *e) {
    int theme=(int)(intptr_t)lv_event_get_user_data(e);
    int index=theme*WATCHFACE_KIND_COUNT+watchface_selected()%WATCHFACE_KIND_COUNT;
    watchface_select(index);settings_set_face((uint8_t)index);settings_save();queue_rebuild();
}
static void pick_lang(lv_event_t *e) {
    settings_set_lang((uint8_t)(uintptr_t)lv_event_get_user_data(e));settings_save();queue_rebuild();
}
static void rebuild(void *arg) {
    (void)arg;if(!s_parent)return;
    if(s_audio_ready && s_page!=SETTINGS_SOUND){audio_out_deinit();s_audio_ready=false;}
    if(s_panel)lv_obj_delete(s_panel);
    s_value=s_slider=s_aod=s_mute=s_face_preview=NULL;s_panel=control_surface(s_parent,0,0,466,466);
    if(s_page==SETTINGS_HOME) {
        launcher_set_title(tr_app_name("settings"));
        char b[80];snprintf(b,sizeof b,word("亮度 %d%% · %s","%d%% brightness · %s"),
            settings_brightness()*100/255,word(settings_idle_mode()==IDLE_AOD?"常显":"自动熄屏",settings_idle_mode()==IDLE_AOD?"Always-on":"Auto off"));
        menu_row(SETTINGS_DISPLAY,word("显示与表盘","Display"),b,112,306);
        snprintf(b,sizeof b,word("音量 %d%% · %s","%d%% volume · %s"),settings_volume(),word(settings_silent()?"静音":"声音开启",settings_silent()?"Muted":"Sound on"));
        menu_row(SETTINGS_SOUND,word("声音","Sound"),b,188,350);
        menu_row(SETTINGS_LANG,word("语言","Language"),settings_lang()?"中文":"English",264,342);
        menu_row(SETTINGS_ABOUT,word("关于设备","About"),"soRound OS",340,270);
    } else if(s_page==SETTINGS_DISPLAY || s_page==SETTINGS_SOUND) {
        bool display=s_page==SETTINGS_DISPLAY;
        launcher_set_title(word(display?"显示":"声音",display?"Display":"Sound"));
        center_text(tr(display?S_BRIGHTNESS:S_VOLUME),110,250,CONTROL_GRAY,control_small_font());
        s_value=center_text("",148,250,CONTROL_WHITE,&lv_font_montserrat_40);update_number();
        s_slider=control_slider(s_panel,210,display?SETTINGS_BRIGHT_MIN:0,display?255:100,
                               display?settings_brightness():settings_volume(),slider_changed,save);
        if(display) {
            menu_row(SETTINGS_FACE,tr(S_FACE),watchface_name(watchface_selected()),262,338);
            bool on=settings_idle_mode()==IDLE_AOD;
            s_aod=control_toggle(s_panel,79,340,308,tr(S_ALWAYS_ON),word(on?"空闲时保持亮屏":"空闲后自动熄屏",on?"Dim while idle":"Turn off when idle"),on,toggle,NULL);
        } else {
            if(!s_audio_ready){audio_out_init();s_audio_ready=true;}
            s_mute=control_toggle(s_panel,62,274,342,tr(S_SILENT),word("提示音与铃声","Alarms and beeps"),settings_silent(),toggle,NULL);
            lv_obj_t *b=control_button(s_panel,145,362,176,48,blip,NULL);control_button_text(b,word("试听","Play sound"));
        }
    } else if(s_page==SETTINGS_FACE) {
        launcher_set_title(tr(S_FACE));int selected=watchface_selected();
        center_text(word("选择即生效","Changes apply instantly"),102,286,CONTROL_GRAY,control_small_font());
        s_face_preview=watchface_create_preview(s_panel,selected);lv_obj_set_pos(s_face_preview,116,134);
        lv_obj_t *prev=control_button(s_panel,52,218,50,50,pick_face,(void *)(intptr_t)-1);
        lv_obj_t *next=control_button(s_panel,364,218,50,50,pick_face,(void *)(intptr_t)1);
        lv_obj_t *l=control_label(prev,LV_SYMBOL_LEFT,UI_FONT_SYM,0,0,30,CONTROL_WHITE);lv_obj_center(l);
        l=control_label(next,LV_SYMBOL_RIGHT,UI_FONT_SYM,0,0,30,CONTROL_WHITE);lv_obj_center(l);
        char caption[64];snprintf(caption,sizeof caption,"%s   %d / %d",watchface_kind_name(selected),selected+1,watchface_count());
        center_text(caption,344,260,CONTROL_WHITE,control_small_font());
        for(int theme=0;theme<WATCHFACE_THEME_COUNT;++theme) {
            lv_obj_t *b=control_button(s_panel,99+theme*92,381,84,38,pick_theme,(void *)(intptr_t)theme);
            lv_obj_set_style_radius(b,12,0);
            if(theme==selected/WATCHFACE_KIND_COUNT){lv_obj_set_style_border_width(b,1,0);lv_obj_set_style_border_color(b,lv_color_hex(COL_RED),0);}
            lv_obj_t *name=control_label(b,watchface_theme_name(theme),&font_wf_18,0,0,76,theme==selected/WATCHFACE_KIND_COUNT?CONTROL_WHITE:CONTROL_GRAY);
            lv_obj_set_style_text_align(name,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(name);
        }
    } else if(s_page==SETTINGS_LANG) {
        launcher_set_title(tr(S_LANGUAGE));
        center_text(word("界面语言","Interface language"),126,280,CONTROL_GRAY,control_small_font());
        for(int i=0;i<2;++i) {
            lv_obj_t *b=control_button(s_panel,83,186+i*92,300,72,pick_lang,(void *)(uintptr_t)i);
            control_button_text(b,i?"中文":"English");
            if(settings_lang()==i){lv_obj_set_style_border_width(b,2,0);lv_obj_set_style_border_color(b,lv_color_hex(COL_RED),0);}
        }
    } else {
        launcher_set_title(word("关于设备","About"));
        center_text("soRound OS",142,300,CONTROL_WHITE,&font_location_24);
        center_text(word("当前固件","Firmware"),207,260,CONTROL_GRAY,control_small_font());
        lv_obj_t *version=center_text(esp_app_get_description()->version,239,282,CONTROL_WHITE,control_small_font());
        lv_label_set_long_mode(version,LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_height(version,LV_SIZE_CONTENT);
        center_text("ESP32-S3 / 466 × 466",337,286,CONTROL_GRAY,control_small_font());
    }
}
static bool settings_back(void) {
    if(s_page==SETTINGS_HOME)return false;
    s_page=s_page==SETTINGS_FACE?SETTINGS_DISPLAY:SETTINGS_HOME;queue_rebuild();return true;
}
static void settings_enter(lv_obj_t *parent) {s_parent=parent;s_page=SETTINGS_HOME;s_audio_ready=false;rebuild(NULL);}
static void settings_exit(void) {
    lv_async_call_cancel(rebuild,NULL);if(s_audio_ready)audio_out_deinit();
    s_parent=s_panel=s_value=s_slider=s_aod=s_mute=s_face_preview=NULL;s_audio_ready=false;
}
static void settings_tick(void) {if(s_page==SETTINGS_FACE&&s_face_preview)watchface_refresh_preview(s_face_preview);}
const app_t app_settings={.name="settings",.color=COL_TXT,.enter=settings_enter,.tick=settings_tick,.exit=settings_exit,.back=settings_back,.tick_period_ms=1000};
