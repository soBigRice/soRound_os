// Real LVGL controllers, only device services are substituted.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "settings.h"
#include "sdk.h"
#include "src/misc/lv_text_private.h"
#include "tools_render.h"
#include "watchface_ui.h"
static uint8_t language,brightness=191,volume=65,idle,silent,face;
static int saves,audio_starts,audio_stops,blips;
static bool sensor_up=true;
static float tx,ty,az=1;
uint8_t settings_lang(void) {return language;}
void settings_set_lang(uint8_t v) {language=v;}
uint8_t settings_brightness(void) {return brightness;}
void settings_set_brightness(uint8_t v) {assert(v>=SETTINGS_BRIGHT_MIN);brightness=v;}
uint8_t settings_volume(void) {return volume;}
void settings_set_volume(uint8_t v) {assert(v<=100);volume=v;}
uint8_t settings_idle_mode(void) {return idle;}
void settings_set_idle_mode(uint8_t v) {idle=v;}
uint8_t settings_silent(void) {return silent;}
void settings_set_silent(uint8_t v) {silent=v;}
void settings_set_face(uint8_t v) {face=v;}
void settings_save(void) {++saves;}
int watchface_count(void) {return 15;}
int watchface_selected(void) {return face;}
const char *watchface_kind_name(int i) {static const char *names[]={"dots","bold","rings","weather","image"};return names[i%5];}
const char *watchface_theme_name(int i) {static const char *names[]={"TYPE","ORBIT","SHIFT"};return names[i];}
const char *watchface_name(int i) {return watchface_kind_name(i);}
void watchface_select(int i) {assert(i>=0 && i<15);face=(uint8_t)i;}
void watchface_refresh_preview(lv_obj_t *preview) {(void)preview;}
void audio_out_init(void) {++audio_starts;}
void audio_out_deinit(void) {++audio_stops;}
void audio_out_set_volume(uint8_t v) {assert(v==volume);}
void audio_out_blip(void) {++blips;}
static const esp_app_desc_t descriptor={.version="v1.7-beta.11-1-g4371a46-dirty"};
const esp_app_desc_t *esp_app_get_description(void) {return &descriptor;}
static lv_obj_t *heading;
void launcher_set_title(const char *t) {lv_label_set_text(heading,t);}
static uint16_t image_pixels[466*466];
static const lv_image_dsc_t face_image={.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=466,.h=466,.stride=932},.data_size=sizeof image_pixels,.data=(const uint8_t *)image_pixels};
const lv_image_dsc_t *img_store_face_image(void) {return &face_image;}
static void preview_draw(lv_event_t *e) {
    watchface_data_t data={.time={.tm_year=126,.tm_mon=9,.tm_mday=5,.tm_wday=1,.tm_hour=10,.tm_min=8},
        .wifi=true,.ssid="soRound",.ip="192.168.1.24",.battery_valid=true,.battery=74,.weather_valid=true,
        .temperature=26,.low=21,.high=28,.humidity=64,.code=3,.image=&face_image};
    lv_area_t a;lv_obj_get_coords(lv_event_get_target_obj(e),&a);
    watchface_render(lv_event_get_layer(e),&a,&data,(int)(intptr_t)lv_event_get_user_data(e),true);
}
lv_obj_t *watchface_create_preview(lv_obj_t *parent,int index) {
    lv_obj_t *o=tools_surface(parent,0,0,233,233);lv_obj_set_style_bg_color(o,lv_color_black(),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_add_event_cb(o,preview_draw,LV_EVENT_DRAW_MAIN,(void *)(intptr_t)index);return o;
}
bool imu_init(void) {return sensor_up;}
bool imu_read_tilt_z(float *x,float *y,float *z) {*x=tx;*y=ty;*z=az;return sensor_up;}
#include "../../main/app_settings.c"
#include "../../main/app_level.c"
#define W 466
_Alignas(LV_DRAW_BUF_ALIGN) static uint16_t buffer[W*W];
static uint16_t pixels[W*W];
static lv_display_t *display;
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *map) {
    int width=lv_area_get_width(a);
    for(int y=a->y1;y<=a->y2;++y) {memcpy(pixels+y*W+a->x1,map,(size_t)width*2);map+=width*2;}
    lv_display_flush_ready(d);
}
static void check_labels(lv_obj_t *o) {
    if(lv_obj_has_flag(o,LV_OBJ_FLAG_HIDDEN))return;
    if(lv_obj_check_type(o,&lv_label_class)) {
        const lv_font_t *font=lv_obj_get_style_text_font(o,0);const char *str=lv_label_get_text(o);uint32_t at=0;
        while(str[at]) {uint32_t cp=lv_text_encoded_next(str,&at);lv_font_glyph_dsc_t glyph;
            assert(lv_font_get_glyph_dsc(font,&glyph,cp,0) && !glyph.is_placeholder);}
        lv_area_t a;lv_obj_get_coords(o,&a);
        for(int i=0;i<4;++i) {
            int x=(i&1)?a.x2:a.x1,y=(i&2)?a.y2:a.y1;
            if(hypot(x-232.5,y-232.5)>225)fprintf(stderr,"label outside circle: %s (%d,%d)\n",str,x,y);
            assert(hypot(x-232.5,y-232.5)<=225);
        }
    }
    for(uint32_t i=0;i<lv_obj_get_child_count(o);++i)check_labels(lv_obj_get_child(o,i));
}
static void capture(lv_obj_t *page,const char *directory,const char *name) {
    lv_obj_update_layout(page);lv_obj_update_layout(heading);check_labels(page);check_labels(heading);
    lv_tick_inc(20);lv_timer_handler();lv_refr_now(display);
    if(!directory)return;
    char file[512];snprintf(file,sizeof file,"%s/%s-%s.ppm",directory,name,language?"zh":"en");
    FILE *f=fopen(file,"wb");assert(f);fprintf(f,"P6\n466 466\n255\n");
    for(int i=0;i<W*W;++i) {uint16_t p=pixels[i];uint8_t rgb[]={((p>>11)&31)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};fwrite(rgb,1,3,f);}
    assert(fclose(f)==0);
}
int main(int argc,char **argv) {
    const char *directory=argc>1?argv[1]:NULL;
    if(argc>2) {FILE *f=fopen(argv[2],"rb");assert(f);assert(fread(image_pixels,1,sizeof image_pixels,f)==sizeof image_pixels);fclose(f);}
    lv_init();i18n_init();display=lv_display_create(W,W);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,buffer,NULL,sizeof buffer,LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
    lv_obj_set_style_bg_color(lv_screen_active(),lv_color_hex(0),0);
    heading=lv_label_create(lv_layer_top());lv_obj_set_style_text_font(heading,&font_location_24,0);
    lv_obj_set_style_text_color(heading,lv_color_hex(COL_TXT),0);lv_obj_align(heading,LV_ALIGN_TOP_MID,0,52);
    tools_test_battery();
    lv_obj_t *back=control_button(lv_layer_top(),101,52,44,44,NULL,NULL);
    lv_obj_t *arrow=control_label(back,LV_SYMBOL_LEFT,UI_FONT_SYM,0,0,20,CONTROL_WHITE);lv_obj_center(arrow);
    for(language=0;language<2;++language) {
        lv_obj_t *page=lv_obj_create(lv_screen_active());lv_obj_remove_style_all(page);lv_obj_set_size(page,W,W);
        settings_enter(page);assert(s_page==SETTINGS_HOME && lv_obj_get_child_count(s_panel)==4);
        assert(!settings_back());capture(page,directory,"settings-home");
        lv_obj_send_event(lv_obj_get_child(s_panel,0),LV_EVENT_CLICKED,NULL);lv_timer_handler();
        assert(s_page==SETTINGS_DISPLAY && lv_slider_get_min_value(s_slider)==64);
        int before=saves;lv_slider_set_value(s_slider,64,LV_ANIM_OFF);lv_obj_send_event(s_slider,LV_EVENT_VALUE_CHANGED,NULL);
        assert(brightness==64 && saves==before);lv_obj_send_event(s_slider,LV_EVENT_RELEASED,NULL);assert(saves==before+1);
        lv_slider_set_value(s_slider,191,LV_ANIM_OFF);lv_obj_send_event(s_slider,LV_EVENT_VALUE_CHANGED,NULL);
        lv_obj_send_event(s_slider,LV_EVENT_RELEASED,NULL);
        before=saves;lv_obj_send_event(lv_obj_get_parent(s_aod),LV_EVENT_CLICKED,NULL);lv_timer_handler();
        assert(idle==IDLE_OFF && saves==before+1 && !lv_obj_has_state(s_aod,LV_STATE_CHECKED));
        assert(settings_back());lv_timer_handler();assert(s_page==SETTINGS_HOME);
        const int pages[]={SETTINGS_HOME,SETTINGS_DISPLAY,SETTINGS_FACE,SETTINGS_SOUND,SETTINGS_LANG,SETTINGS_ABOUT};
        for(unsigned i=0;i<sizeof pages/sizeof pages[0];++i) {
            s_page=pages[i];rebuild(NULL);char name[40];snprintf(name,sizeof name,"settings-%u",i);capture(page,directory,name);
            if(s_page==SETTINGS_SOUND) {
                before=saves;lv_slider_set_value(s_slider,100,LV_ANIM_OFF);lv_obj_send_event(s_slider,LV_EVENT_VALUE_CHANGED,NULL);
                assert(volume==100 && saves==before);lv_obj_send_event(s_slider,LV_EVENT_RELEASED,NULL);assert(saves==before+1);
                before=saves;lv_obj_send_event(lv_obj_get_parent(s_mute),LV_EVENT_CLICKED,NULL);lv_timer_handler();
                assert(silent && saves==before+1);blip(NULL);assert(blips>0);
            }
        }
        assert(audio_stops==audio_starts);
        s_page=SETTINGS_FACE;
        for(face=0;face<15;++face) {rebuild(NULL);char name[32];snprintf(name,sizeof name,"face-%d",face);capture(page,directory,name);}
        face=2;rebuild(NULL);before=saves;
        lv_obj_send_event(lv_obj_get_child(s_panel,6),LV_EVENT_CLICKED,NULL);lv_timer_handler();assert(face==7&&saves==before+1);
        lv_obj_send_event(lv_obj_get_child(s_panel,7),LV_EVENT_CLICKED,NULL);lv_timer_handler();assert(face==12&&saves==before+2);
        face=14;rebuild(NULL);lv_obj_send_event(lv_obj_get_child(s_panel,3),LV_EVENT_CLICKED,NULL);lv_timer_handler();assert(face==0);
        assert(settings_back());lv_timer_handler();assert(s_page==SETTINGS_DISPLAY);
        assert(settings_back());lv_timer_handler();assert(s_page==SETTINGS_HOME);
        s_page=SETTINGS_SOUND;rebuild(NULL);assert(audio_starts==audio_stops+1);
        queue_rebuild();settings_exit();lv_obj_delete(page);lv_timer_handler();assert(!s_panel && audio_stops==audio_starts);
        idle=0;silent=0;face=0;
    }
    // Every direction, near-vertical and inverted readings stay bounded and recover.
    language=0;lv_obj_t *page=lv_obj_create(lv_screen_active());lv_obj_remove_style_all(page);lv_obj_set_size(page,W,W);
    level_enter(page);
    const float dirs[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{.7f,.7f,.01f},{0,0,-1}};
    for(unsigned i=0;i<6;++i) {
        tx=dirs[i][0];ty=dirs[i][1];az=dirs[i][2];
        for(int n=0;n<30;++n) {lv_tick_inc(20);level_tick();}
        assert(isfinite(ox) && isfinite(oy) && hypotf(ox,oy)<=MAXR+.01f);
        char name[32];snprintf(name,sizeof name,"level-%u",i);capture(page,directory,name);
    }
    sensor_up=false;for(int i=0;i<3;++i) {lv_tick_inc(20);level_tick();}
    assert(lv_obj_has_flag(g_ball,LV_OBJ_FLAG_HIDDEN));sensor_up=true;tx=ty=0;az=1;
    lv_tick_inc(20);level_tick();assert(!lv_obj_has_flag(g_ball,LV_OBJ_FLAG_HIDDEN));
    tx=NAN;for(int i=0;i<3;++i) {lv_tick_inc(20);level_tick();}assert(lv_obj_has_flag(g_ball,LV_OBJ_FLAG_HIDDEN));
    level_exit();lv_obj_delete(page);
    // Approved layout: check actual glyph ink, including the superscript, against full tick bounds.
    // Reuse the same system overlay for the approved level layout.
    lv_obj_set_style_radius(back,LV_RADIUS_CIRCLE,0);lv_obj_set_style_pad_all(back,0,0);
    for(language=0;language<2;++language) {
        lv_label_set_text(heading,tools_text("LEVEL","水平仪"));tools_header(heading,back,arrow);
        page=tools_surface(lv_screen_active(),0,0,466,466);sensor_up=true;tx=ty=0;az=1;level_enter(page);
        const float poses[][3]={{0,0,1},{.1391731f,-.0697565f,.987815f},{.9961947f,0,.0871557f},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
        const char *names[]={"level-flat","level-nine","level-85","level-90","level-left","level-down","level-up"};
        for(unsigned i=0;i<sizeof poses/sizeof poses[0];++i) {
            tx=poses[i][0];ty=poses[i][1];az=poses[i][2];
            for(int n=0;n<60;++n){lv_tick_inc(20);level_tick();}
            capture(page,directory,names[i]);
            lv_area_t state,number,unit,target;lv_obj_get_coords(g_status,&state);lv_obj_get_coords(g_big,&number);
            lv_obj_get_coords(g_unit,&unit);lv_obj_get_coords(g_target,&target);
            assert(target.y2<=324 && state.y1>target.y2); // Includes 13px outward ticks and their caps.
            const lv_font_t *font=&font_tools_60;lv_font_glyph_dsc_t glyph;
            assert(lv_font_get_glyph_dsc(font,&glyph,(uint32_t)lv_label_get_text(g_big)[0],0));
            int number_top=number.y1+font->line_height-font->base_line-glyph.box_h-glyph.ofs_y;
            assert(number_top>state.y2);
            font=&font_tools_24;assert(lv_font_get_glyph_dsc(font,&glyph,0xb0,0));
            int unit_top=unit.y1+font->line_height-font->base_line-glyph.box_h-glyph.ofs_y;
            assert(unit_top>state.y2);
            assert(number.x2+7<=unit.x1 && abs((number.x1+unit.x2)/2-233)<=1);
            assert(hypotf(ox,oy)<=81.01f && hypotf(ox,oy)+BALL_R<DISH);
        }
        sensor_up=false;for(int n=0;n<3;++n){lv_tick_inc(20);level_tick();}capture(page,directory,"level-fault");
        assert(lv_obj_has_flag(g_content,LV_OBJ_FLAG_HIDDEN));
        sensor_up=true;tx=ty=0;az=1;lv_tick_inc(20);level_tick();assert(lv_obj_has_flag(g_fault,LV_OBJ_FLAG_HIDDEN));
        level_exit();lv_obj_delete(page);
    }
    puts("Settings categories/nested back/15 native previews/theme switching/wrap EN/ZH, round layout/glyphs, slider persistence, whole-row toggles, audio cleanup; level all directions/edge/stale/fault recovery passed");
}
