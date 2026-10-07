// Direct category navigation. Returning from a detail goes to Settings, not the launcher.
#include "control_ui.h"
#include "settings.h"
#include "watchface.h"
#include "watchface_ui.h"
#include "glyph.h"
#include "audio_out.h"
#include "esp_app_desc.h"
#include "img_store.h"
#include "identity_ui.h"
#include "identity_geometry.h"
#include <stdio.h>

enum { SETTINGS_HOME, SETTINGS_DISPLAY, SETTINGS_FACE, SETTINGS_SOUND, SETTINGS_LANG, SETTINGS_ABOUT };
static lv_obj_t *s_parent,*s_panel,*s_value,*s_slider,*s_aod,*s_mute,*s_face_preview;
static lv_obj_t *s_home_list;
static int32_t s_home_y;
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
typedef struct {lv_obj_t *obj;lv_layer_t *layer;int x,y;} settings_ink_t;
static void home_line(settings_ink_t *ink,int x1,int y1,int x2,int y2,uint32_t color) {
    lv_draw_line_dsc_t line;lv_draw_line_dsc_init(&line);
    line.base.obj=ink->obj;line.width=2;line.color=lv_color_hex(color);line.round_start=line.round_end=true;
    line.p1=(lv_point_precise_t){ink->x+x1,ink->y+y1};line.p2=(lv_point_precise_t){ink->x+x2,ink->y+y2};
    lv_draw_line(ink->layer,&line);
}
static void home_arc(settings_ink_t *ink,int x,int y,int radius,int start,int end,uint32_t color) {
    lv_draw_arc_dsc_t arc;lv_draw_arc_dsc_init(&arc);
    arc.base.obj=ink->obj;arc.center=(lv_point_t){ink->x+x,ink->y+y};arc.radius=radius;
    arc.start_angle=start;arc.end_angle=end;arc.width=2;arc.color=lv_color_hex(color);arc.rounded=true;
    lv_draw_arc(ink->layer,&arc);
}
static void home_icon_draw(lv_event_t *event) {
    lv_obj_t *obj=lv_event_get_target_obj(event);lv_area_t area;lv_obj_get_coords(obj,&area);
    settings_ink_t ink={obj,lv_event_get_layer(event),area.x1,area.y1};
    int kind=(int)(intptr_t)lv_event_get_user_data(event);
    if(kind<0) {
        home_line(&ink,2,9,8,15,0x858d91);home_line(&ink,8,15,2,21,0x858d91);
    } else if(kind==SETTINGS_DISPLAY) {
        lv_draw_rect_dsc_t frame;lv_draw_rect_dsc_init(&frame);
        frame.base.obj=obj;frame.bg_opa=LV_OPA_TRANSP;frame.border_width=2;
        frame.border_color=lv_color_hex(CONTROL_WHITE);frame.radius=4;
        lv_area_t screen={ink.x+3,ink.y+6,ink.x+32,ink.y+26};lv_draw_rect(ink.layer,&frame,&screen);
        home_line(&ink,18,27,18,31,CONTROL_WHITE);home_line(&ink,12,31,24,31,CONTROL_WHITE);
        home_line(&ink,9,21,15,21,IDENTITY_RED);
    } else if(kind==SETTINGS_SOUND) {
        const int points[][2]={{4,14},{10,14},{18,8},{18,28},{10,22},{4,22},{4,14}};
        for(unsigned i=1;i<sizeof points/sizeof points[0];++i)
            home_line(&ink,points[i-1][0],points[i-1][1],points[i][0],points[i][1],CONTROL_WHITE);
        home_arc(&ink,16,18,15,320,40,IDENTITY_RED);
    } else if(kind==SETTINGS_LANG) {
        home_arc(&ink,18,18,14,0,360,CONTROL_WHITE);
        home_line(&ink,18,5,18,31,CONTROL_WHITE);
        home_line(&ink,6,18,30,18,IDENTITY_RED);
        home_line(&ink,8,10,28,10,CONTROL_WHITE);home_line(&ink,8,26,28,26,CONTROL_WHITE);
    }
}
static void home_icon(lv_obj_t *parent,int kind,int x,int y) {
    lv_obj_t *icon=control_surface(parent,x,y,kind<0?12:36,kind<0?30:36);
    ui_obj_set_clickable(icon,false);
    lv_obj_add_event_cb(icon,home_icon_draw,LV_EVENT_DRAW_MAIN,(void *)(intptr_t)kind);
}
static void home_scroll_draw(lv_event_t *event) {
    if(!s_home_list || s_page!=SETTINGS_HOME)return;
    int32_t y=lv_obj_get_scroll_y(s_home_list),total=y+lv_obj_get_scroll_bottom(s_home_list);
    if(total<=0)return;
    lv_obj_t *obj=lv_event_get_target_obj(event);lv_area_t area;lv_obj_get_coords(obj,&area);
    settings_ink_t ink={obj,lv_event_get_layer(event),area.x1,area.y1};
    int height=lv_obj_get_height(s_home_list),thumb=100*height/(height+total);
    int start=310+(100-thumb)*LV_CLAMP(0,y,total)/total;
    home_arc(&ink,233,233,209,310,410,0x252d31);
    home_arc(&ink,233,233,209,start,start+thumb,IDENTITY_RED);
}
static void home_scrolled(lv_event_t *event) {
    if(!s_home_list || lv_event_get_target_obj(event)!=s_home_list)return;
    s_home_y=lv_obj_get_scroll_y(s_home_list);
    lv_area_t area;lv_obj_get_coords(s_panel,&area);
    lv_area_t dirty={area.x1+363,area.y1+65,area.x1+445,area.y1+402};
    lv_obj_invalidate_area(s_panel,&dirty);
}
static void home_row(int page,const char *name,const char *detail) {
    // A centered, fixed-width row stays inside the round surface throughout scrolling.
    lv_obj_t *row=control_button(s_home_list,0,0,278,88,open_page,(void *)(intptr_t)page);
    lv_obj_set_style_bg_opa(row,LV_OPA_TRANSP,0);
    lv_obj_set_style_bg_opa(row,LV_OPA_COVER,LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(row,lv_color_hex(0x171b1d),LV_STATE_PRESSED);
    if(page==SETTINGS_ABOUT) {
        lv_obj_t *logo=identity_logo_create(row,36);if(logo)lv_obj_set_pos(logo,0,26);
    } else home_icon(row,page,0,25);
    control_label(row,name,&font_location_24,52,10,206,CONTROL_WHITE);
    control_label(row,detail,control_small_font(),52,46,206,0x8b9398);
    home_icon(row,-1,261,29);
    if(page!=SETTINGS_ABOUT) {
        lv_obj_t *line=control_surface(row,52,87,220,1);
        ui_obj_set_clickable(line,false);lv_obj_set_style_bg_opa(line,LV_OPA_COVER,0);
        lv_obj_set_style_bg_color(line,lv_color_hex(0x202629),0);
    }
}

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
    if(s_home_list)s_home_y=lv_obj_get_scroll_y(s_home_list);
    int32_t home_y=s_home_y;s_home_list=NULL;
    if(s_audio_ready && s_page!=SETTINGS_SOUND){audio_out_deinit();s_audio_ready=false;}
    if(s_panel)lv_obj_delete(s_panel);
    s_value=s_slider=s_aod=s_mute=s_face_preview=NULL;s_panel=control_surface(s_parent,0,0,466,466);
    if(s_page==SETTINGS_HOME) {
        launcher_set_title(tr_app_name("settings"));
        s_home_list=control_surface(s_panel,0,110,466,298);
        ui_obj_set_scrollable(s_home_list,true);lv_obj_set_scroll_dir(s_home_list,LV_DIR_VER);
        ui_obj_set_scroll_chain_hor(s_home_list,false);ui_obj_set_scroll_chain_ver(s_home_list,false);
        ui_obj_set_scroll_elastic(s_home_list,false);ui_obj_set_scroll_momentum(s_home_list,true);
        lv_obj_set_scrollbar_mode(s_home_list,LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_flex_flow(s_home_list,LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(s_home_list,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_ver(s_home_list,14,0);lv_obj_set_style_pad_row(s_home_list,16,0);
        char b[80];snprintf(b,sizeof b,"%d%% · %s",
            settings_brightness()*100/255,word(settings_idle_mode()==IDLE_AOD?"常显":"自动熄屏",settings_idle_mode()==IDLE_AOD?"Always-on":"Auto off"));
        home_row(SETTINGS_DISPLAY,word("显示与表盘","Display"),b);
        snprintf(b,sizeof b,"%d%% · %s",settings_volume(),word(settings_silent()?"静音":"声音开启",settings_silent()?"Muted":"Sound on"));
        home_row(SETTINGS_SOUND,word("声音","Sound"),b);
        home_row(SETTINGS_LANG,word("语言","Language"),settings_lang()?"中文":"English");
        home_row(SETTINGS_ABOUT,word("关于本机","About"),"soRound OS");
        lv_obj_update_layout(s_home_list);
        lv_obj_add_event_cb(s_home_list,home_scrolled,LV_EVENT_SCROLL,NULL);
        lv_obj_add_event_cb(s_panel,home_scroll_draw,LV_EVENT_DRAW_POST,NULL);
        lv_obj_scroll_to_y(s_home_list,home_y,LV_ANIM_OFF);s_home_y=lv_obj_get_scroll_y(s_home_list);
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
        launcher_set_title(word("关于本机","About"));
        lv_obj_t *logo=identity_logo_create(s_panel,140);
        if(logo)lv_obj_set_pos(logo,163,105);
        lv_obj_t *name=center_text("soRound OS",243,300,IDENTITY_WHITE,&font_identity_34);
        lv_obj_set_style_text_letter_space(name,-1,0);
        lv_obj_t *version=center_text(esp_app_get_description()->version,296,282,0xa1a4a6,control_small_font());
        lv_label_set_long_mode(version,LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_height(version,LV_SIZE_CONTENT);
        // Leave room for a two-line, 31-byte descriptor version without colliding with hardware.
        lv_obj_t *line=control_surface(s_panel,180,347,106,1);
        lv_obj_set_style_bg_color(line,lv_color_hex(0x24282a),0);lv_obj_set_style_bg_opa(line,LV_OPA_COVER,0);
        ui_obj_set_clickable(line,false);
        center_text("ESP32-S3",357,240,0xa1a4a6,control_small_font());
        center_text("466 × 466 AMOLED",388,220,0x757b7e,&font_identity_16);
    }
}
static bool settings_back(void) {
    if(s_page==SETTINGS_HOME)return false;
    s_page=s_page==SETTINGS_FACE?SETTINGS_DISPLAY:SETTINGS_HOME;queue_rebuild();return true;
}
static void settings_enter(lv_obj_t *parent) {s_parent=parent;s_page=SETTINGS_HOME;s_home_y=0;s_audio_ready=false;rebuild(NULL);}
static void settings_exit(void) {
    lv_async_call_cancel(rebuild,NULL);if(s_audio_ready)audio_out_deinit();
    s_parent=s_panel=s_value=s_slider=s_aod=s_mute=s_face_preview=s_home_list=NULL;s_home_y=0;s_audio_ready=false;
}
static void settings_tick(void) {if(s_page==SETTINGS_FACE&&s_face_preview)watchface_refresh_preview(s_face_preview);}
const app_t app_settings={.name="settings",.color=COL_TXT,.enter=settings_enter,.tick=settings_tick,.exit=settings_exit,.back=settings_back,.tick_period_ms=1000};
