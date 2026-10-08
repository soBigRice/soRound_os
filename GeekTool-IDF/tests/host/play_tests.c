// Real LVGL, dye solver and maze/particle controllers. Only the clock, IMU,
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
#include "../../main/fluid_ink.c"
#include "../../main/app_maze.c"
#define W 466
static uint16_t pixels[W*W];
_Alignas(LV_DRAW_BUF_ALIGN) static uint16_t buffer[W*48];
static lv_display_t *display;
static lv_indev_data_t pointer={.state=LV_INDEV_STATE_RELEASED};
static lv_obj_t *page;
static const char *folder;
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *data){
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
static void brush(int x1,int y1,int x2,int y2){
    touch_at(x1,y1,true);for(int i=1;i<=16;++i)touch_at(x1+(x2-x1)*i/16,y1+(y2-y1)*i/16,true);touch_at(x2,y2,false);
}
static void solver_boundaries(void){
    fluid_ink_t *ink=malloc(fluid_ink_bytes()),*copy=malloc(fluid_ink_bytes());
    uint16_t *guarded=calloc(W*W+16,sizeof(uint16_t));assert(guarded);
    for(int i=0;i<8;++i)guarded[i]=guarded[W*W+8+i]=0x5aa5;
    fluid_ink_dirty_t dirty[FLUID_INK_BANDS];
    for(unsigned palette=0;palette<3;++palette){
        fluid_ink_reset(ink,palette);memcpy(copy,ink,fluid_ink_bytes());
        fluid_ink_inject(ink,NAN,.5f,1,1,0,1);fluid_ink_inject(ink,.5f,.5f,INFINITY,1,0,1);
        fluid_ink_inject(ink,-1,2,1,1,0,1);assert(!memcmp(copy,ink,fluid_ink_bytes()));
        for(int frame=0;frame<180;++frame){
            float angle=frame*.29f;
            fluid_ink_inject(ink,.5f+cosf(angle)*.38f,.5f+sinf(angle)*.38f,cosf(angle)*10000,sinf(angle)*10000,frame,.8f);
            assert(isfinite(fluid_ink_step(ink,frame%2?.02f:10)));
            for(int k=0;k<FC;++k){assert(isfinite(ink->u[k])&&isfinite(ink->v[k]));for(int c=0;c<3;++c)assert(isfinite(ink->dye[c][k])&&ink->dye[c][k]>=0);}
        }
        assert(fluid_ink_render(ink,guarded+8,dirty));
        for(int i=0;i<FLUID_INK_BANDS;++i)if(dirty[i].x1<=dirty[i].x2)assert(dirty[i].x1>=0&&dirty[i].y1>=0&&dirty[i].x2<W&&dirty[i].y2<W);
        assert(!fluid_ink_render(ink,guarded+8,dirty));
        for(int i=0;i<8;++i)assert(guarded[i]==0x5aa5&&guarded[W*W+8+i]==0x5aa5);
    }
    free(guarded);free(copy);free(ink);
    puts("Dye solver: nonfinite/outside inputs, extreme stirring/elapsed time, three palettes, bounded output and stable repeated render passed.");
}
static void particle_and_ink(void){
    for(language=0;language<2;++language){
        i18n_init();g_mode=0;page=new_page();fluid_enter(page);assert(g_p&&g_timer&&g_pm_held&&live==2);capture(language?"particles-zh":"particles-en");
        tap(290,107);assert(g_mode==1&&g_ink&&!g_p&&live==2&&g_timer&&g_pm_held);
        brush(125,183,343,288);brush(353,283,133,325);brush(133,313,268,153);
        for(int i=0;i<20;++i)advance(20);
        capture(language?"ink-zh":"ink-en");
        tap(292,401);assert(!g_ink_playing&&lv_timer_get_paused(g_timer)&&!g_pm_held);
        uint16_t *copy=malloc(W*W*2);memcpy(copy,g_buf,W*W*2);advance(1000);assert(!memcmp(copy,g_buf,W*W*2));
        fluid_visibility(false);fluid_visibility(true);assert(lv_timer_get_paused(g_timer)&&!g_pm_held);
        brush(150,240,315,245);assert(memcmp(copy,g_buf,W*W*2)&&!g_pm_held);free(copy);
        for(int i=0;i<3;++i){unsigned before=g_palette;tap(177,401);assert(g_palette==(before+1)%3);capture(language?"ink-palette-zh":"ink-palette-en");}
        tap(292,401);assert(g_ink_playing&&!lv_timer_get_paused(g_timer)&&g_pm_held);
        fluid_visibility(false);assert(lv_timer_get_paused(g_timer)&&!g_pm_held);host_time_us+=10000000;fluid_visibility(true);assert(!lv_timer_get_paused(g_timer)&&g_pm_held&&g_last_us==host_time_us);
        // A settled field must sleep, then wake under a slow accumulated change of tilt.
        memset(g_ink->u,0,sizeof(g_ink->u));memset(g_ink->v,0,sizeof(g_ink->v));tilt_x=tilt_y=0;
        for(int i=0;i<42;++i)advance(20);
        assert(g_asleep&&!g_pm_held);
        for(int i=1;i<=10;++i){tilt_x=.006f*i;advance(80);}assert(!g_asleep&&g_pm_held);
        tilt_x=0;tap(176,107);assert(g_mode==0&&g_p&&!g_ink&&live==2);tap(290,107);assert(g_ink&&live==2);
        fluid_exit();assert(!g_timer&&!g_pm);lv_obj_delete(page);assert(!live);advance(20);
    }
    sensor=false;g_mode=1;page=new_page();fluid_enter(page);assert(g_ink&&!g_has_imu);brush(140,240,320,240);fluid_exit();lv_obj_delete(page);assert(!live);sensor=true;
    for(unsigned mode=0;mode<2;++mode)for(unsigned failure=1;failure<=2;++failure){
        allocations=0;fail_at=failure;g_mode=mode;page=new_page();fluid_enter(page);assert(!g_timer&&!g_pm_held&&live==0);capture("allocation-failure");
        fail_at=0;tap(mode?176:290,107);assert(g_timer&&live==2);fluid_exit();lv_obj_delete(page);assert(!live);
    }
    puts("Both fluid modes EN/ZH: real drag, palette, pause/draw, no IMU, settled sleep/slow wake, visibility and allocation/switch/exit cleanup passed.");
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
        assert(!maze_back());tap(332,363);assert(g_level==11&&g_ball);sensor=false;host_time_us+=20000;maze_tick();assert(!strcmp(lv_label_get_text(g_msg),tr(S_FLUID_NOIMU)));sensor=true;
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
        if(i==15){float angle=(10+(8+36/60.0f)/60.0f)*PI/6;
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
    solver_boundaries();particle_and_ink();maze_levels_and_physics();native_hands();lv_deinit();return 0;
}
