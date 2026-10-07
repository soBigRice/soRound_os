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
const char *watchface_name(int i) {static char name[40];snprintf(name,sizeof name,"%s / %s",watchface_theme_name(i/5),watchface_kind_name(i));return name;}
void watchface_select(int i) {assert(i>=0 && i<15);face=(uint8_t)i;}
void watchface_refresh_preview(lv_obj_t *preview) {(void)preview;}
void audio_out_init(void) {++audio_starts;}
void audio_out_deinit(void) {++audio_stops;}
void audio_out_set_volume(uint8_t v) {assert(v==volume);}
void audio_out_blip(void) {++blips;}
static esp_app_desc_t descriptor={.version="v1.7-beta.29"};
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
static lv_indev_t *touch;
static lv_indev_data_t touch_data={.state=LV_INDEV_STATE_RELEASED};
static int right_backs;
static void read_touch(lv_indev_t *indev,lv_indev_data_t *data) {(void)indev;*data=touch_data;}
static void touch_at(int x,int y,bool down) {
    touch_data.point=(lv_point_t){x,y};touch_data.state=down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
    lv_tick_inc(20);lv_timer_handler();
}
static void drag(int x1,int y1,int x2,int y2) {
    touch_at(x1,y1,true);
    for(int step=1;step<=6;++step)touch_at(x1+(x2-x1)*step/6,y1+(y2-y1)*step/6,true);
    touch_at(x2,y2,false);
}
static void settle(void) {for(int i=0;i<60;++i){lv_tick_inc(20);lv_timer_handler();}}
static void back_tapped(lv_event_t *event) {(void)event;settings_back();}
static void back_gesture(lv_event_t *event) {
    (void)event;
    if(lv_indev_get_gesture_dir(lv_indev_active())==LV_DIR_RIGHT) {
        ++right_backs;settings_back();
    }
}
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
            bool covered=lv_font_get_glyph_dsc(font,&glyph,cp,0);
            if(!covered||glyph.is_placeholder)fprintf(stderr,"missing glyph U+%04X in '%s'\n",(unsigned)cp,str);
            assert(covered && !glyph.is_placeholder);}
        lv_area_t a;lv_obj_get_coords(o,&a);
        // Scrolled-out labels still require all glyphs; only their painted intersection has screen bounds.
        bool visible=true;
        for(lv_obj_t *parent=lv_obj_get_parent(o);parent;parent=lv_obj_get_parent(parent)) {
            if(lv_obj_has_flag(parent,LV_OBJ_FLAG_OVERFLOW_VISIBLE))continue;
            lv_area_t clip;lv_obj_get_coords(parent,&clip);
            a.x1=LV_MAX(a.x1,clip.x1);a.y1=LV_MAX(a.y1,clip.y1);
            a.x2=LV_MIN(a.x2,clip.x2);a.y2=LV_MIN(a.y2,clip.y2);
            if(a.x1>a.x2 || a.y1>a.y2){visible=false;break;}
        }
        for(int i=0;visible && i<4;++i) {
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
static lv_obj_t *settings_screen(void) {
    // Match launcher enter_app: gestures terminate at a parentless, non-scrolling screen.
    lv_obj_t *page=lv_obj_create(NULL);lv_obj_remove_style_all(page);lv_obj_set_size(page,W,W);
    lv_obj_set_style_bg_color(page,lv_color_black(),0);lv_obj_set_style_bg_opa(page,LV_OPA_COVER,0);
    ui_obj_set_scrollable(page,false);lv_screen_load(page);return page;
}
static lv_obj_t *button_named(lv_obj_t *root,const char *text) {
    if(lv_obj_check_type(root,&lv_label_class) && strcmp(lv_label_get_text(root),text)==0) {
        lv_obj_t *parent=lv_obj_get_parent(root);
        if(lv_obj_check_type(parent,&lv_button_class))return parent;
    }
    for(uint32_t i=0;i<lv_obj_get_child_count(root);++i) {
        lv_obj_t *button=button_named(lv_obj_get_child(root,i),text);if(button)return button;
    }
    return NULL;
}
static void tap(lv_obj_t *obj) {
    lv_obj_update_layout(obj);lv_area_t area;lv_obj_get_coords(obj,&area);
    for(int corner=0;corner<4;++corner)assert(hypot(((corner&1)?area.x2:area.x1)-232.5,((corner&2)?area.y2:area.y1)-232.5)<=233);
    touch_at((area.x1+area.x2)/2,(area.y1+area.y2)/2,true);
    touch_at((area.x1+area.x2)/2,(area.y1+area.y2)/2,false);
}
static void check_full_text(lv_obj_t *obj) {
    if(lv_obj_check_type(obj,&lv_label_class)) {
        lv_point_t measured;lv_text_get_size(&measured,lv_label_get_text(obj),lv_obj_get_style_text_font(obj,0),
            lv_obj_get_style_text_letter_space(obj,0),lv_obj_get_style_text_line_space(obj,0),lv_obj_get_width(obj),LV_TEXT_FLAG_NONE);
        if(measured.y>lv_obj_get_height(obj))fprintf(stderr,"truncated detail: %s\n",lv_label_get_text(obj));
        assert(measured.y<=lv_obj_get_height(obj));
    }
    for(uint32_t i=0;i<lv_obj_get_child_count(obj);++i)check_full_text(lv_obj_get_child(obj,i));
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
    lv_obj_t *back=control_button(lv_layer_top(),101,52,44,44,back_tapped,NULL);
    lv_obj_t *arrow=control_label(back,LV_SYMBOL_LEFT,UI_FONT_SYM,0,0,20,CONTROL_WHITE);lv_obj_center(arrow);
    touch=lv_indev_create();lv_indev_set_type(touch,LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(touch,display);lv_indev_set_read_cb(touch,read_touch);
    // One native timer sample per step; manual event reads also resume a timer and duplicate stationary samples.
    lv_timer_set_period(lv_indev_get_read_timer(touch),20);lv_indev_set_gesture_min_distance(touch,64);
    lv_obj_t *stage=lv_screen_active();
    const int destinations[]={SETTINGS_BRIGHTNESS,SETTINGS_FACE,SETTINGS_AOD,SETTINGS_VOLUME,SETTINGS_MUTE,SETTINGS_LANG,SETTINGS_ABOUT};
    const char *names[]={"brightness","face","aod","volume","mute","language","about"};
    for(language=0;language<2;++language) {
        brightness=191;volume=65;idle=IDLE_AOD;silent=0;face=0;
        snprintf(descriptor.version,sizeof descriptor.version,"v1.7-beta.29");
        lv_obj_t *page=settings_screen();lv_obj_add_event_cb(page,back_gesture,LV_EVENT_GESTURE,NULL);
        settings_enter(page);assert(s_page==SETTINGS_HOME && lv_obj_get_child_count(s_home_list)==7);
        assert(!settings_back());capture(page,directory,"settings-home");
        lv_area_t title_before,title_after,viewport,last;
        lv_obj_get_coords(heading,&title_before);lv_obj_get_coords(s_home_list,&viewport);
        lv_obj_get_coords(lv_obj_get_child(s_home_list,6),&last);assert(last.y1>viewport.y2);
        int maximum=lv_obj_get_scroll_bottom(s_home_list),stored=saves;assert(maximum>0);
        drag(220,200,220,130);settle();assert(s_page==SETTINGS_HOME && saves==stored && lv_obj_get_scroll_y(s_home_list)>0);
        lv_obj_scroll_to_y(s_home_list,maximum,LV_ANIM_OFF);capture(page,directory,"settings-home-bottom");
        lv_obj_get_coords(heading,&title_after);assert(memcmp(&title_before,&title_after,sizeof title_before)==0);
        lv_obj_get_coords(lv_obj_get_child(s_home_list,6),&last);assert(last.y1>=viewport.y1 && last.y2<=viewport.y2);
        drag(220,230,220,295);settle();assert(s_page==SETTINGS_HOME && saves==stored && lv_obj_get_scroll_y(s_home_list)<maximum);
        // Every row is a direct entry: opening it must not modify the setting itself.
        for(unsigned item=0;item<sizeof destinations/sizeof destinations[0];++item) {
            lv_obj_t *row=lv_obj_get_child(s_home_list,item);lv_obj_scroll_to_view(row,LV_ANIM_OFF);lv_obj_update_layout(s_home_list);
            int position=lv_obj_get_scroll_y(s_home_list);stored=saves;tap(row);settle();
            assert(s_page==destinations[item] && saves==stored && !s_home_list);
            char name[64];snprintf(name,sizeof name,"settings-%s",names[item]);capture(page,directory,name);check_full_text(s_panel);
            if(s_page==SETTINGS_BRIGHTNESS || s_page==SETTINGS_VOLUME) {
                bool is_brightness=s_page==SETTINGS_BRIGHTNESS;int minimum=is_brightness?64:0,limit=is_brightness?255:100;
                assert(s_slider && !s_aod && !s_mute && !s_face_preview && !s_scroll);
                assert(lv_slider_get_min_value(s_slider)==minimum && lv_slider_get_max_value(s_slider)==limit);
                lv_slider_set_value(s_slider,minimum,LV_ANIM_OFF);lv_obj_send_event(s_slider,LV_EVENT_VALUE_CHANGED,NULL);assert(saves==stored);
                lv_area_t slider;lv_obj_get_coords(s_slider,&slider);int y=(slider.y1+slider.y2)/2,end=slider.x2+8;
                // Move the thumb past the track end; x2 is the last inclusive pixel, not full travel.
                touch_at(slider.x1,y,true);
                for(int step=1;step<=6;++step)touch_at(slider.x1+(end-slider.x1)*step/6,y,true);
                assert((is_brightness?brightness:volume)==limit && saves==stored && s_page==destinations[item]);
                touch_at(end,y,false);assert(saves==stored+1);
                if(!is_brightness) {
                    assert(audio_starts==audio_stops+1);int heard=blips;
                    tap(button_named(s_panel,word("试听","Play sound")));assert(blips==heard+1 && saves==stored+1);
                }
                lv_slider_set_value(s_slider,is_brightness?191:65,LV_ANIM_OFF);lv_obj_send_event(s_slider,LV_EVENT_VALUE_CHANGED,NULL);
                lv_obj_send_event(s_slider,LV_EVENT_RELEASED,NULL);assert(saves==stored+2);
                stored=saves;drag(233,320,233,285);settle();assert(!s_scroll && s_page==destinations[item] && saves==stored);
            } else if(s_page==SETTINGS_AOD || s_page==SETTINGS_MUTE) {
                bool is_aod=s_page==SETTINGS_AOD;lv_obj_t *sw=is_aod?s_aod:s_mute;
                assert(sw && !s_slider && !s_face_preview && !s_scroll && audio_starts==audio_stops);
                bool original=lv_obj_has_state(sw,LV_STATE_CHECKED);tap(sw);settle();
                sw=is_aod?s_aod:s_mute;assert(lv_obj_has_state(sw,LV_STATE_CHECKED)!=original && saves==stored+1);
                assert((is_aod?(idle==IDLE_AOD):(silent!=0))!=original);
                snprintf(name,sizeof name,"settings-%s-toggled",names[item]);capture(page,directory,name);check_full_text(s_panel);
                tap(sw);settle();assert(saves==stored+2 && (is_aod?(idle==IDLE_AOD):(silent!=0))==original);
            } else if(s_page==SETTINGS_FACE) {
                assert(!s_scroll && s_face_preview && !s_slider && !s_aod && !s_mute);
                lv_area_t preview;lv_obj_get_coords(s_face_preview,&preview);
                assert(preview.x1==116 && preview.y1==116 && lv_area_get_width(&preview)==233 && lv_area_get_height(&preview)==233);
                // Vertical drags leave the preview, caption and controls in place and do not select.
                drag(233,260,233,180);settle();lv_area_t after;lv_obj_get_coords(s_face_preview,&after);
                assert(memcmp(&preview,&after,sizeof preview)==0 && saves==stored && face==0 && s_page==SETTINGS_FACE);
                face=2;rebuild(NULL);tap(button_named(s_panel,"ORBIT"));settle();assert(face==7 && saves==stored+1);
                tap(button_named(s_panel,"SHIFT"));settle();assert(face==12 && saves==stored+2);
                face=14;rebuild(NULL);tap(button_named(s_panel,LV_SYMBOL_RIGHT));settle();assert(face==0 && saves==stored+3);
                tap(button_named(s_panel,LV_SYMBOL_LEFT));settle();assert(face==14 && saves==stored+4);
                for(face=0;face<15;++face) {
                    rebuild(NULL);snprintf(name,sizeof name,"face-%u",face);capture(page,directory,name);check_full_text(s_panel);
                    lv_obj_get_coords(s_face_preview,&after);assert(memcmp(&preview,&after,sizeof preview)==0);
                }
                face=0;rebuild(NULL);
            } else if(s_page==SETTINGS_LANG) {
                // LVGL reports negative bottom space when content is shorter than the viewport.
                assert(s_scroll && lv_obj_get_scroll_bottom(s_scroll)<=0 && lv_obj_get_scroll_y(s_scroll)==0);uint8_t original=language;
                tap(button_named(s_panel,original?"English":"中文"));settle();assert(language!=original && saves==stored+1 && s_page==SETTINGS_LANG);
                tap(button_named(s_panel,original?"中文":"English"));settle();assert(language==original && saves==stored+2);
            } else if(s_page==SETTINGS_ABOUT) {
                const char *versions[]={"v1.7-beta.29","MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM"};
                for(unsigned v=0;v<2;++v) {
                    snprintf(descriptor.version,sizeof descriptor.version,"%s",versions[v]);rebuild(NULL);
                    lv_obj_scroll_to_y(s_scroll,0,LV_ANIM_OFF);capture(page,directory,v?"about-long":"about-native");check_full_text(s_panel);
                    lv_obj_t *version=lv_obj_get_child(s_body,2),*chip=lv_obj_get_child(s_body,4);
                    assert(strcmp(lv_label_get_text(version),versions[v])==0 && lv_label_get_long_mode(version)==LV_LABEL_LONG_MODE_WRAP);
                    lv_area_t a,b;lv_obj_get_coords(version,&a);lv_obj_get_coords(chip,&b);assert(a.y2<b.y1);
                    lv_obj_get_coords(heading,&title_before);drag(104,210,104,130);settle();
                    assert(s_page==SETTINGS_ABOUT && saves==stored && lv_obj_get_scroll_y(s_scroll)>0);
                    maximum=lv_obj_get_scroll_y(s_scroll)+lv_obj_get_scroll_bottom(s_scroll);
                    lv_obj_scroll_to_y(s_scroll,maximum/2,LV_ANIM_OFF);capture(page,NULL,"about-middle");
                    lv_obj_scroll_to_y(s_scroll,maximum,LV_ANIM_OFF);capture(page,directory,v?"about-long-bottom":"about-native-bottom");
                    lv_obj_get_coords(heading,&title_after);assert(memcmp(&title_before,&title_after,sizeof title_before)==0);
                    drag(104,260,104,330);settle();assert(lv_obj_get_scroll_y(s_scroll)<maximum && s_page==SETTINGS_ABOUT);
                }
                snprintf(descriptor.version,sizeof descriptor.version,"v1.7-beta.29");
            }
            // The list position survives either the physical back button or a right swipe.
            stored=saves;int returned=right_backs;
            if(s_page==SETTINGS_AOD) {tap(back);settle();assert(right_backs==returned);}
            else {drag(104,315,224,315);settle();assert(right_backs==returned+1);}
            assert(s_page==SETTINGS_HOME && saves==stored && lv_obj_get_scroll_y(s_home_list)==position);
            assert(audio_starts==audio_stops);
        }
        capture(page,directory,"settings-home-return");
        s_page=SETTINGS_VOLUME;rebuild(NULL);assert(audio_starts==audio_stops+1);
        queue_rebuild();settings_exit();lv_screen_load(stage);lv_obj_delete(page);settle();assert(!s_panel && audio_starts==audio_stops);
    }
    // Both scrolling screens can be destroyed during inertia; every adjustment can exit with a rebuild queued.
    language=0;
    for(int cycle=0;cycle<4;++cycle) {
        for(int which=0;which<2;++which) {
            lv_obj_t *page=settings_screen();settings_enter(page);assert(lv_obj_get_scroll_y(s_home_list)==0);
            if(which) {s_page=SETTINGS_ABOUT;rebuild(NULL);}
            drag(104,210,104,130);assert(lv_obj_get_scroll_y(s_scroll)>0);
            settings_exit();lv_screen_load(stage);lv_obj_delete(page);settle();
            assert(!s_scroll && !s_body && !lv_indev_get_scroll_obj(touch));
        }
    }
    for(unsigned item=0;item<sizeof destinations/sizeof destinations[0];++item) {
        lv_obj_t *page=settings_screen();settings_enter(page);s_page=destinations[item];rebuild(NULL);queue_rebuild();
        settings_exit();lv_screen_load(stage);lv_obj_delete(page);settle();settings_tick();
        assert(!s_panel && !s_scroll && !s_face_preview && audio_starts==audio_stops);
    }
    lv_indev_delete(touch);touch=NULL;
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
    puts("Seven direct settings entries EN/ZH, home scrolling/position restore, centered fixed 15-face selection, independent sliders/switches/language, real pointer persistence/back/preview taps, long about text, audio/inertia/queued-exit cleanup passed; level directions/stale/fault recovery passed");
}
