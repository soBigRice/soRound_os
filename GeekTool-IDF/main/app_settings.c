// One setting per detail. The list scrolls; adjustment screens stay centered on the round display.
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

enum { SETTINGS_HOME, SETTINGS_BRIGHTNESS, SETTINGS_FACE, SETTINGS_AOD,
       SETTINGS_VOLUME, SETTINGS_MUTE, SETTINGS_LANG, SETTINGS_ABOUT, SETTINGS_COUNT };
static lv_obj_t *s_parent,*s_panel,*s_value,*s_slider,*s_aod,*s_mute,*s_face_preview;
static lv_obj_t *s_home_list,*s_scroll,*s_body;
static int32_t s_scroll_y[SETTINGS_COUNT];
static int s_built_page;
static int s_page;
static bool s_audio_ready;
static const char *word(const char *zh,const char *en) {return settings_lang()?zh:en;}
static void rebuild(void *arg);
static void queue_rebuild(void) {lv_async_call_cancel(rebuild,NULL);lv_async_call(rebuild,NULL);}
static lv_obj_t *stack_text(const char *value,int width,uint32_t color,const lv_font_t *font) {
    lv_obj_t *l=control_label(s_body,value,font,0,0,width,color);
    lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);
    lv_label_set_long_mode(l,LV_LABEL_LONG_MODE_WRAP);lv_obj_set_height(l,LV_SIZE_CONTENT);return l;
}
static lv_obj_t *center_text(const char *value,int y,int width,uint32_t color,const lv_font_t *font) {
    lv_obj_t *l=control_label(s_panel,value,font,(466-width)/2,y,width,color);
    lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);
    lv_label_set_long_mode(l,LV_LABEL_LONG_MODE_WRAP);lv_obj_set_height(l,LV_SIZE_CONTENT);return l;
}
static void open_page(lv_event_t *e) {
    int page=(int)(intptr_t)lv_event_get_user_data(e);
    s_scroll_y[page]=0;s_page=page;queue_rebuild();
}
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
    } else if(kind==SETTINGS_BRIGHTNESS) {
        lv_draw_rect_dsc_t frame;lv_draw_rect_dsc_init(&frame);
        frame.base.obj=obj;frame.bg_opa=LV_OPA_TRANSP;frame.border_width=2;
        frame.border_color=lv_color_hex(CONTROL_WHITE);frame.radius=4;
        lv_area_t screen={ink.x+3,ink.y+6,ink.x+32,ink.y+26};lv_draw_rect(ink.layer,&frame,&screen);
        home_line(&ink,18,27,18,31,CONTROL_WHITE);home_line(&ink,12,31,24,31,CONTROL_WHITE);
        home_line(&ink,9,21,15,21,IDENTITY_RED);
    } else if(kind==SETTINGS_VOLUME || kind==SETTINGS_MUTE) {
        const int points[][2]={{4,14},{10,14},{18,8},{18,28},{10,22},{4,22},{4,14}};
        for(unsigned i=1;i<sizeof points/sizeof points[0];++i)
            home_line(&ink,points[i-1][0],points[i-1][1],points[i][0],points[i][1],CONTROL_WHITE);
        if(kind==SETTINGS_MUTE)home_line(&ink,24,13,32,23,IDENTITY_RED);
        else home_arc(&ink,16,18,15,320,40,IDENTITY_RED);
    } else if(kind==SETTINGS_FACE) {
        home_arc(&ink,18,18,14,0,360,CONTROL_WHITE);
        home_line(&ink,18,9,18,18,CONTROL_WHITE);home_line(&ink,18,18,25,22,IDENTITY_RED);
    } else if(kind==SETTINGS_AOD) {
        home_arc(&ink,18,18,10,0,360,CONTROL_WHITE);
        for(int angle=0;angle<360;angle+=45) {
            int x=lv_trigo_cos(angle),y=lv_trigo_sin(angle);
            home_line(&ink,18+x*14/32767,18+y*14/32767,18+x*17/32767,18+y*17/32767,angle?CONTROL_WHITE:IDENTITY_RED);
        }
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
static void scroll_draw(lv_event_t *event) {
    if(!s_scroll)return;
    int32_t y=lv_obj_get_scroll_y(s_scroll),total=y+lv_obj_get_scroll_bottom(s_scroll);
    if(total<=0)return;
    lv_obj_t *obj=lv_event_get_target_obj(event);lv_area_t area;lv_obj_get_coords(obj,&area);
    settings_ink_t ink={obj,lv_event_get_layer(event),area.x1,area.y1};
    int height=lv_obj_get_height(s_scroll),thumb=100*height/(height+total);
    int start=310+(100-thumb)*LV_CLAMP(0,y,total)/total;
    home_arc(&ink,233,233,209,310,410,0x252d31);
    home_arc(&ink,233,233,209,start,start+thumb,IDENTITY_RED);
}
static void scrolled(lv_event_t *event) {
    if(!s_scroll || lv_event_get_target_obj(event)!=s_scroll)return;
    s_scroll_y[s_built_page]=lv_obj_get_scroll_y(s_scroll);
    lv_area_t area;lv_obj_get_coords(s_panel,&area);
    lv_area_t dirty={area.x1+363,area.y1+65,area.x1+445,area.y1+402};
    lv_obj_invalidate_area(s_panel,&dirty);
}
static void scroll_view(void) {
    s_scroll=control_surface(s_panel,0,110,466,298);
    ui_obj_set_scrollable(s_scroll,true);lv_obj_set_scroll_dir(s_scroll,LV_DIR_VER);
    ui_obj_set_scroll_chain_hor(s_scroll,false);ui_obj_set_scroll_chain_ver(s_scroll,false);
    ui_obj_set_scroll_elastic(s_scroll,false);ui_obj_set_scroll_momentum(s_scroll,true);
    lv_obj_set_scrollbar_mode(s_scroll,LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(s_scroll,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_scroll,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
}
static void detail_body(void) {
    scroll_view();s_body=control_surface(s_scroll,0,0,278,LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_body,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_body,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(s_body,14,0);lv_obj_set_style_pad_row(s_body,24,0);
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

static void update_number(void) {
    char b[12];int value=s_page==SETTINGS_BRIGHTNESS?settings_brightness()*100/255:settings_volume();
    snprintf(b,sizeof b,"%d%%",value);lv_label_set_text(s_value,b);
}
static void slider_changed(lv_event_t *e) {
    int v=lv_slider_get_value(lv_event_get_target_obj(e));
    if(s_page==SETTINGS_BRIGHTNESS)settings_set_brightness((uint8_t)v);
    else {settings_set_volume((uint8_t)v);audio_out_set_volume((uint8_t)v);}
    update_number();
}
static void save(lv_event_t *e) {(void)e;settings_save();}
static void blip(lv_event_t *e) {(void)e;audio_out_blip();}
static void toggle(lv_event_t *e) {
    bool on=lv_obj_has_state(lv_event_get_target_obj(e),LV_STATE_CHECKED);
    if(s_page==SETTINGS_AOD)settings_set_idle_mode(on?IDLE_AOD:IDLE_OFF);
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
    if(s_scroll)s_scroll_y[s_built_page]=lv_obj_get_scroll_y(s_scroll);
    s_home_list=s_scroll=s_body=NULL;s_built_page=s_page;
    if(s_audio_ready && s_page!=SETTINGS_VOLUME){audio_out_deinit();s_audio_ready=false;}
    if(s_panel)lv_obj_delete(s_panel);
    s_value=s_slider=s_aod=s_mute=s_face_preview=NULL;s_panel=control_surface(s_parent,0,0,466,466);
    if(s_page==SETTINGS_HOME) {
        launcher_set_title(tr_app_name("settings"));
        scroll_view();s_home_list=s_scroll;
        lv_obj_set_style_pad_ver(s_home_list,14,0);lv_obj_set_style_pad_row(s_home_list,16,0);
        char b[80];snprintf(b,sizeof b,"%d%%",settings_brightness()*100/255);
        home_row(SETTINGS_BRIGHTNESS,word("亮度","Brightness"),b);
        home_row(SETTINGS_FACE,word("表盘","Watch face"),watchface_name(watchface_selected()));
        home_row(SETTINGS_AOD,word("常显","Always-on"),tr(settings_idle_mode()==IDLE_AOD?S_ON:S_OFF));
        snprintf(b,sizeof b,"%d%%",settings_volume());
        home_row(SETTINGS_VOLUME,word("音量","Volume"),b);
        home_row(SETTINGS_MUTE,word("静音","Silent"),tr(settings_silent()?S_ON:S_OFF));
        home_row(SETTINGS_LANG,word("语言","Language"),settings_lang()?"中文":"English");
        home_row(SETTINGS_ABOUT,word("关于本机","About"),"soRound OS");
    } else if(s_page==SETTINGS_BRIGHTNESS || s_page==SETTINGS_VOLUME) {
        bool display=s_page==SETTINGS_BRIGHTNESS;
        launcher_set_title(word(display?"亮度":"音量",display?"Brightness":"Volume"));
        s_value=center_text("",150,278,CONTROL_WHITE,&lv_font_montserrat_40);update_number();
        s_slider=control_slider(s_panel,239,display?SETTINGS_BRIGHT_MIN:0,display?255:100,
                               display?settings_brightness():settings_volume(),slider_changed,save);
        char minimum[8];snprintf(minimum,sizeof minimum,"%d%%",display?SETTINGS_BRIGHT_MIN*100/255:0);
        control_label(s_panel,minimum,control_small_font(),90,294,80,CONTROL_GRAY);
        lv_obj_t *maximum=control_label(s_panel,"100%",control_small_font(),296,294,80,CONTROL_GRAY);
        lv_obj_set_style_text_align(maximum,LV_TEXT_ALIGN_RIGHT,0);
        if(!display) {
            if(!s_audio_ready){audio_out_init();s_audio_ready=true;}
            lv_obj_t *b=control_button(s_panel,123,344,220,56,blip,NULL);control_button_text(b,word("试听","Play sound"));
        }
    } else if(s_page==SETTINGS_AOD || s_page==SETTINGS_MUTE) {
        bool aod=s_page==SETTINGS_AOD,on=aod?settings_idle_mode()==IDLE_AOD:settings_silent();
        launcher_set_title(word(aod?"常显":"静音",aod?"Always-on":"Silent"));
        center_text(tr(on?S_ON:S_OFF),138,278,CONTROL_WHITE,&font_location_24);
        lv_obj_t *sw=lv_switch_create(s_panel);lv_obj_set_size(sw,90,52);lv_obj_set_pos(sw,188,207);
        lv_obj_set_style_bg_color(sw,lv_color_hex(CONTROL_LINE),LV_PART_MAIN);
        lv_obj_set_style_bg_color(sw,lv_color_hex(COL_RED),LV_PART_INDICATOR|LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(sw,lv_color_hex(CONTROL_WHITE),LV_PART_KNOB);
        ui_obj_set_gesture_bubble(sw,false);ui_obj_set_event_bubble(sw,false);
        if(on)lv_obj_add_state(sw,LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw,toggle,LV_EVENT_VALUE_CHANGED,NULL);
        if(aod)s_aod=sw;else s_mute=sw;
        center_text(word(aod?(on?"空闲时保持亮屏":"空闲后自动熄屏"):"提示音与铃声",
                         aod?(on?"Dim while idle":"Turn off when idle"):"Alarms and beeps"),300,278,CONTROL_GRAY,control_small_font());
    } else if(s_page==SETTINGS_FACE) {
        launcher_set_title(word("表盘","Watch face"));int selected=watchface_selected();
        // The preview center is the physical screen center, independent of list scroll position.
        s_face_preview=watchface_create_preview(s_panel,selected);lv_obj_set_pos(s_face_preview,116,116);
        lv_obj_t *prev=control_button(s_panel,52,205,50,56,pick_face,(void *)(intptr_t)-1);
        lv_obj_t *next=control_button(s_panel,364,205,50,56,pick_face,(void *)(intptr_t)1);
        lv_obj_t *l=control_label(prev,LV_SYMBOL_LEFT,UI_FONT_SYM,0,0,30,CONTROL_WHITE);lv_obj_center(l);
        l=control_label(next,LV_SYMBOL_RIGHT,UI_FONT_SYM,0,0,30,CONTROL_WHITE);lv_obj_center(l);
        char caption[64];snprintf(caption,sizeof caption,"%s   %d / %d",watchface_kind_name(selected),selected+1,watchface_count());
        center_text(caption,354,278,CONTROL_WHITE,control_small_font());
        for(int theme=0;theme<WATCHFACE_THEME_COUNT;++theme) {
            lv_obj_t *b=control_button(s_panel,113+theme*84,385,72,44,pick_theme,(void *)(intptr_t)theme);
            lv_obj_set_style_radius(b,12,0);
            if(theme==selected/WATCHFACE_KIND_COUNT){lv_obj_set_style_border_width(b,1,0);lv_obj_set_style_border_color(b,lv_color_hex(COL_RED),0);}
            lv_obj_t *name=control_label(b,watchface_theme_name(theme),&font_wf_18,0,0,64,theme==selected/WATCHFACE_KIND_COUNT?CONTROL_WHITE:CONTROL_GRAY);
            lv_obj_set_style_text_align(name,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(name);
        }
    } else if(s_page==SETTINGS_LANG) {
        launcher_set_title(tr(S_LANGUAGE));detail_body();
        stack_text(word("界面语言","Interface language"),278,CONTROL_GRAY,control_small_font());
        for(int i=0;i<2;++i) {
            lv_obj_t *b=control_button(s_body,0,0,278,80,pick_lang,(void *)(uintptr_t)i);
            control_button_text(b,i?"中文":"English");
            if(settings_lang()==i){lv_obj_set_style_border_width(b,1,0);lv_obj_set_style_border_color(b,lv_color_hex(COL_RED),0);}
        }
    } else {
        launcher_set_title(word("关于本机","About"));detail_body();
        lv_obj_set_style_pad_row(s_body,20,0);
        identity_logo_create(s_body,140);
        lv_obj_t *name=stack_text("soRound OS",278,IDENTITY_WHITE,&font_identity_34);
        lv_obj_set_style_text_letter_space(name,-1,0);
        stack_text(esp_app_get_description()->version,278,0xa1a4a6,control_small_font());
        // Content-sized rows let the full descriptor version push hardware down without overlap.
        lv_obj_t *line=control_surface(s_body,0,0,106,1);
        lv_obj_set_style_bg_color(line,lv_color_hex(0x24282a),0);lv_obj_set_style_bg_opa(line,LV_OPA_COVER,0);
        ui_obj_set_clickable(line,false);
        stack_text("ESP32-S3",278,0xa1a4a6,control_small_font());
        stack_text("466 × 466 AMOLED",278,0x757b7e,&font_identity_16);
    }
    if(s_scroll) {
        lv_obj_update_layout(s_scroll);
        lv_obj_add_event_cb(s_scroll,scrolled,LV_EVENT_SCROLL,NULL);
        lv_obj_add_event_cb(s_panel,scroll_draw,LV_EVENT_DRAW_POST,NULL);
        lv_obj_scroll_to_y(s_scroll,s_scroll_y[s_page],LV_ANIM_OFF);
        s_scroll_y[s_page]=lv_obj_get_scroll_y(s_scroll);
    }
}
static bool settings_back(void) {
    if(s_page==SETTINGS_HOME)return false;
    s_page=SETTINGS_HOME;queue_rebuild();return true;
}
static void settings_enter(lv_obj_t *parent) {
    s_parent=parent;s_page=SETTINGS_HOME;s_audio_ready=false;
    for(unsigned i=0;i<sizeof s_scroll_y/sizeof s_scroll_y[0];++i)s_scroll_y[i]=0;
    rebuild(NULL);
}
static void settings_exit(void) {
    lv_async_call_cancel(rebuild,NULL);if(s_audio_ready)audio_out_deinit();
    s_parent=s_panel=s_value=s_slider=s_aod=s_mute=s_face_preview=s_home_list=s_scroll=s_body=NULL;
    for(unsigned i=0;i<sizeof s_scroll_y/sizeof s_scroll_y[0];++i)s_scroll_y[i]=0;
    s_audio_ready=false;
}
static void settings_tick(void) {if(s_page==SETTINGS_FACE&&s_face_preview)watchface_refresh_preview(s_face_preview);}
const app_t app_settings={.name="settings",.color=COL_TXT,.enter=settings_enter,.tick=settings_tick,.exit=settings_exit,.back=settings_back,.tick_period_ms=1000};
