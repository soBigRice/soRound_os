// Real LVGL and maze/particle controllers. Only the clock, IMU,
// allocator failure and PM APIs are fixtures; pointers drive the actual widgets.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "settings.h"
#include "esp_heap_caps.h"
#include "esp_pm.h"
#include "src/misc/lv_text_private.h"
int64_t host_time_us=1000000;
static uint8_t language;
static bool sensor=true;
static float tilt_x,tilt_y;
uint8_t settings_lang(void){return language;}
void settings_set_lang(uint8_t value){language=value;}
void settings_save(void){}
bool imu_init(void){return sensor;}
bool imu_read_tilt(float *x,float *y){*x=tilt_x;*y=tilt_y;return sensor;}
static unsigned allocations,live,fail_at;
static void *owned[16];
static void *test_malloc(size_t size){
    if(++allocations==fail_at)return NULL;
    void *p=malloc(size);assert(p);
    unsigned i=0;while(i<16&&owned[i])++i;assert(i<16);owned[i]=p;++live;return p;
}
static void test_free(void *p){
    if(!p)return;
    unsigned i=0;while(i<16&&owned[i]!=p)++i;assert(i<16);owned[i]=NULL;--live;free(p);
}
static void *test_heap_malloc(size_t size,unsigned caps){(void)caps;return test_malloc(size);}
#define malloc test_malloc
#define free test_free
#define heap_caps_malloc test_heap_malloc
#define heap_caps_free test_free
#include "../../main/app_fluid.c"
#undef malloc
#undef free
#undef heap_caps_malloc
#undef heap_caps_free
#include "../../main/app_maze.c"
#define W 466
static uint16_t pixels[W*W];
static unsigned flushed_pixels;
_Alignas(LV_DRAW_BUF_ALIGN) static uint16_t buffer[W*48];
static lv_display_t *display;
static lv_indev_data_t pointer={.state=LV_INDEV_STATE_RELEASED};
static lv_obj_t *page;
static const char *folder;
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *data){
    flushed_pixels+=(unsigned)(lv_area_get_width(a)*lv_area_get_height(a));
    int w=lv_area_get_width(a);for(int y=a->y1;y<=a->y2;++y){memcpy(pixels+y*W+a->x1,data,w*2);data+=w*2;}lv_display_flush_ready(d);
}
static void read_pointer(lv_indev_t *i,lv_indev_data_t *data){(void)i;*data=pointer;}
static void advance(unsigned ms){host_time_us+=ms*1000;lv_tick_inc(ms);lv_timer_handler();}
static void touch_at(int x,int y,bool down){pointer.point=(lv_point_t){x,y};pointer.state=down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;advance(20);}
static void tap(int x,int y){touch_at(x,y,true);touch_at(x,y,false);advance(20);}
static lv_obj_t *new_page(void){
    lv_obj_t *p=lv_obj_create(lv_screen_active());lv_obj_remove_style_all(p);lv_obj_set_size(p,W,W);ui_obj_set_scrollable(p,false);return p;
}
static void check_labels(lv_obj_t *o){
    if(lv_obj_check_type(o,&lv_label_class)){
        const char *value=lv_label_get_text(o);const lv_font_t *font=lv_obj_get_style_text_font(o,0);uint32_t position=0;
        while(value[position]){uint32_t code=lv_text_encoded_next(value,&position);lv_font_glyph_dsc_t glyph={0};
            bool valid=lv_font_get_glyph_dsc(font,&glyph,code,0)&&!glyph.is_placeholder;
            if(!valid)fprintf(stderr,"missing glyph U+%04X in %s\n",(unsigned)code,value);
            assert(valid);
        }
    }
    for(unsigned i=0;i<lv_obj_get_child_count(o);++i)check_labels(lv_obj_get_child(o,i));
}
static void capture(const char *name){
    lv_obj_update_layout(page);lv_refr_now(display);check_labels(page);if(!folder)return;
    char filename[1024];snprintf(filename,sizeof filename,"%s/%s.ppm",folder,name);FILE *f=fopen(filename,"wb");assert(f);fprintf(f,"P6\n466 466\n255\n");
    for(int i=0;i<W*W;++i){uint16_t p=pixels[i];uint8_t rgb[]={((p>>11)&31)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};assert(fwrite(rgb,1,3,f)==3);}assert(fclose(f)==0);
}
static void particles(void){
    for(language=0;language<2;++language){
        i18n_init();page=new_page();fluid_enter(page);
        assert(g_p&&g_timer&&g_pm_held&&live==2);
        // A single canvas and hint: the removed mode and dye controls cannot return.
        assert(lv_obj_get_child_count(page)==2);
        capture(language?"particles-zh":"particles-en");
        part_t *original=g_p;
        tap(290,107);tap(177,401);tap(292,401);
        assert(g_p==original&&live==2&&!lv_timer_get_paused(g_timer));
        fluid_visibility(false);assert(lv_timer_get_paused(g_timer)&&!g_pm_held);
        uint16_t *copy=malloc(BW*BW*2);assert(copy);memcpy(copy,g_buf,BW*BW*2);
        advance(1000);assert(!memcmp(copy,g_buf,BW*BW*2));free(copy);
        fluid_visibility(true);
        assert(!lv_timer_get_paused(g_timer)&&g_pm_held&&g_last_us==host_time_us&&g_remainder==0);
        fluid_exit();assert(!g_timer&&!g_pm&&!g_p&&!g_buf);
        // Buffers stay alive until the canvas DELETE event, including deferred screen deletion.
        assert(live==2);lv_obj_delete(page);assert(!live);advance(20);
    }
    sensor=false;page=new_page();fluid_enter(page);
    assert(g_p&&!g_has_imu&&!strcmp(lv_label_get_text(g_hint),tr(S_FLUID_NOIMU)));
    for(int i=0;i<20;++i)advance(20);
    for(int i=0;i<NPART;++i)assert(isfinite(g_p[i].x)&&isfinite(g_p[i].y));
    fluid_exit();lv_obj_delete(page);assert(!live);sensor=true;
    for(unsigned failure=1;failure<=2;++failure){
        allocations=0;fail_at=failure;page=new_page();fluid_enter(page);
        assert(!g_timer&&!g_pm&&!g_pm_held&&live==0);
        assert(!strcmp(lv_label_get_text(g_hint),settings_lang()?"内存不足":"Not enough memory"));
        capture("allocation-failure");fluid_exit();lv_obj_delete(page);assert(!live);
        fail_at=0;page=new_page();fluid_enter(page);assert(g_timer&&live==2);
        fluid_exit();lv_obj_delete(page);assert(!live);
    }
    puts("Particles EN/ZH: direct entry, no mode controls, no IMU, visibility clock/PM, allocation failure and deferred exit cleanup passed.");
}
static int route_for(const maze_level_t *l,int route[36]){
    int prev[36],queue[36],head=0,tail=1,n=l->size;for(int i=0;i<n*n;++i)prev[i]=-1;prev[0]=0;queue[0]=0;
    while(head<tail){int k=queue[head++],x=k%n,y=k/n;const int next[]={y?k-n:-1,x<n-1?k+1:-1,y<n-1?k+n:-1,x?k-1:-1};
        for(int d=0;d<4;++d){if(next[d]<0){assert(l->walls[k]&(1<<d));continue;}
            assert(!!(l->walls[k]&(1<<d))==!!(l->walls[next[d]]&(1<<((d+2)%4))));
            if(!(l->walls[k]&(1<<d))&&prev[next[d]]<0){prev[next[d]]=k;queue[tail++]=next[d];}
        }
    }
    assert(tail==n*n);int path[36],count=0,k=n*n-1;do{path[count++]=k;if(k==0)break;k=prev[k];}while(count<36);
    assert(count==l->steps+1);for(int i=0;i<count;++i)route[i]=path[count-i-1];return count;
}
static void maze_levels_and_physics(void){
    for(language=0;language<2;++language){i18n_init();page=new_page();maze_enter(page);capture(language?"maze-menu-zh":"maze-menu-en");
        for(int index=0;index<12;++index){
            assert(g_maze_mode==MAZE_MENU);tap(134+index%4*66,177+index/4*93);assert(g_level==index&&g_maze_mode==MAZE_PLAY&&g_ball&&s_nwall<MAXW);
            char name[32];snprintf(name,sizeof name,"maze-%02d-%s",index+1,language?"zh":"en");capture(name);
            int route[36],count=route_for(current_level(),route);float cell=BOARD/(float)current_level()->size;
            for(int i=1;i<count;++i){float x=OX+(route[i]%current_level()->size+.5f)*cell,y=OY+(route[i]/current_level()->size+.5f)*cell;
                int iterations=0;while(hypotf(bx-x,by-y)>1.5f&&g_maze_mode==MAZE_PLAY&&iterations++<600){
                    tilt_x=clampf(((x-bx)*30-vx*11)/(G_MS2*PPM),-1,1);tilt_y=clampf(((y-by)*30-vy*11)/(G_MS2*PPM),-1,1);
                    host_time_us+=20000;maze_tick();
                }
                assert(iterations<600);
            }
            assert(g_maze_mode==MAZE_WIN&&(g_completed&(1<<index)));tilt_x=tilt_y=0;advance(20);capture(language?"maze-win-zh":"maze-win-en");
            // Completion must wait for a real next/replay action instead of replacing the map.
            host_time_us+=3000000;maze_tick();assert(g_maze_mode==MAZE_WIN&&g_level==index);
            if(index==0||index==11){
                tap(233,346);
                if(index==0){assert(g_maze_mode==MAZE_PLAY&&g_level==1);assert(maze_back());advance(20);}
                else assert(g_maze_mode==MAZE_MENU&&g_level==11);
            }else{
                tap(233,397);assert(g_maze_mode==MAZE_PLAY&&g_level==index&&vx==0&&vy==0);
                assert(maze_back());advance(20);
            }
            assert(g_maze_mode==MAZE_MENU);
        }
        assert(!maze_back());tap(332,363);assert(g_level==11&&g_ball);sensor=false;host_time_us+=20000;maze_tick();assert(!strcmp(lv_label_get_text(g_msg),tr(S_FLUID_NOIMU)));
        // IMU故障且提示不变时应完全静止；恢复后文字仍要更新。
        // An unchanged IMU failure must stay idle, while sensor recovery still updates the message.
        lv_refr_now(display);flushed_pixels=0;
        for(int n=0;n<100;n++){host_time_us+=20000;maze_tick();lv_refr_now(display);}
        assert(flushed_pixels==0);sensor=true;host_time_us+=20000;maze_tick();lv_refr_now(display);
        assert(flushed_pixels>0 && !strcmp(lv_label_get_text(g_msg),maze_word("Tilt into the red dot","倾斜设备，滚入红点")));
        maze_back();maze_exit();lv_obj_delete(page);advance(20);assert(!g_maze_queued&&!g_maze_parent);
    }
    puts("12 authored levels EN/ZH: native pointer selection/replay/next/final-level/back, reciprocal/closed walls, connected maps, actual continuous collision goals, fixed win state and queued-exit cleanup passed.");
}
static void hands_capture_draw(lv_event_t *e);
static void native_hands(void){
    page=new_page();
    for(int i=15;i<21;++i){lv_obj_t *preview=lv_obj_create(page);lv_obj_remove_style_all(preview);lv_obj_set_size(preview,466,466);ui_obj_set_scrollable(preview,false);
        // The event callback below invokes the exact production renderer.
        lv_obj_add_event_cb(preview,hands_capture_draw,LV_EVENT_DRAW_MAIN,(void *)(uintptr_t)i);
        char name[32];snprintf(name,sizeof name,"hand-%02d",i-15);capture(name);
        if(i==15){float angle=(10+(8+36/60.0f)/60.0f)*3.14159265358979323846f/6;
            for(int length=40;length<90;++length)for(int offset=-3;offset<=3;++offset){
                int x=(int)lroundf(233+sinf(angle)*length+cosf(angle)*offset);
                int y=(int)lroundf(233-cosf(angle)*length+sinf(angle)*offset);uint16_t p=pixels[y*W+x];
                assert(((p>>11)&31)>26&&((p>>5)&63)>52&&(p&31)>26);
            }
        }
        lv_obj_delete(preview);
    }
    lv_obj_delete(page);
}
static void hands_capture_draw(lv_event_t *e){
    watchface_data_t data={.time={.tm_year=126,.tm_mon=9,.tm_mday=8,.tm_wday=4,.tm_hour=10,.tm_min=8,.tm_sec=36},.battery_valid=true,.battery=74};
    lv_area_t a;lv_obj_get_coords(lv_event_get_target_obj(e),&a);watchface_render(lv_event_get_layer(e),&a,&data,(uintptr_t)lv_event_get_user_data(e),false);
}
int main(int argc,char **argv){
    folder=argc>1?argv[1]:NULL;lv_init();i18n_init();display=lv_display_create(W,W);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
    lv_obj_set_style_bg_color(lv_screen_active(),lv_color_black(),0);lv_indev_t *input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(input,read_pointer);
    lv_timer_set_period(lv_indev_get_read_timer(input),20);
    particles();maze_levels_and_physics();native_hands();lv_deinit();return 0;
}
