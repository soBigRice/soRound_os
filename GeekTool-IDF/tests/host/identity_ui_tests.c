// Real native logo renderer and animation scheduler. No hardware services or raster logo.
#include "identity_ui.h"
#include "identity_geometry.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 466
_Alignas(LV_DRAW_BUF_ALIGN) static uint16_t buffer[W*48];
static uint16_t pixels[W*W],reference[160*160];
static lv_display_t *display;
static void flush(lv_display_t *d,const lv_area_t *area,uint8_t *bytes) {
    int width=lv_area_get_width(area);
    for(int y=area->y1;y<=area->y2;++y){memcpy(pixels+y*W+area->x1,bytes,(size_t)width*2);bytes+=width*2;}
    lv_display_flush_ready(d);
}
static void draw(void){lv_obj_update_layout(lv_screen_active());lv_obj_update_layout(lv_layer_top());lv_refr_now(display);}
static void export(const char *folder,const char *name) {
    if(!folder)return;
    char path[1024];snprintf(path,sizeof path,"%s/%s.ppm",folder,name);
    FILE *file=fopen(path,"wb");assert(file);fprintf(file,"P6\n466 466\n255\n");
    for(int i=0;i<W*W;++i){uint16_t p=pixels[i];uint8_t rgb[]={((p>>11)&31)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};fwrite(rgb,1,3,file);}
    assert(fclose(file)==0);
}
static bool red(uint16_t p){return ((p>>11)&31)>20 && ((p>>5)&63)<25 && (p&31)<12;}
static int white_count(void) {
    int count=0;for(int y=153;y<313;++y)for(int x=153;x<313;++x){uint16_t p=pixels[y*W+x];if(((p>>11)&31)>20 && ((p>>5)&63)>45 && (p&31)>20)++count;}
    return count;
}
static void check_dot(double expected_x,double expected_y) {
    int count=0;double x=0,y=0;
    for(int row=153;row<313;++row)for(int col=153;col<313;++col)if(red(pixels[row*W+col])){x+=col;y+=row;++count;}
    assert(count>400 && count<800);assert(fabs(x/count-expected_x)<1.5 && fabs(y/count-expected_y)<1.5);
}
static void advance(int ms){lv_tick_inc((uint32_t)ms);lv_timer_handler();draw();}
static void move_original(void *obj,int32_t x){lv_obj_set_x(obj,x);}
int main(int argc,char **argv) {
    const char *folder=argc>1?argv[1]:NULL;
    lv_init();display=lv_display_create(W,W);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,buffer,NULL,sizeof buffer,LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
    lv_obj_set_style_bg_color(lv_screen_active(),lv_color_black(),0);
    assert(!identity_logo_create(lv_screen_active(),0));
    lv_obj_t *static_logo=identity_logo_create(lv_screen_active(),160);assert(static_logo);
    lv_obj_set_pos(static_logo,153,153);draw();check_dot(278.5,187.5);
    // The hole remains transparent, and the gap between the ring and dot stays black.
    assert(pixels[239*W+228]==0 && pixels[199*W+267]==0);
    for(int y=0;y<160;++y)memcpy(reference+y*160,pixels+(153+y)*W+153,160*sizeof(uint16_t));
    export(folder,"logo-native");lv_obj_delete(static_logo);draw();
    // The original ready page stays alive beneath the startup cover.
    lv_obj_t *original=lv_label_create(lv_screen_active());lv_label_set_text(original,"Ready");lv_obj_align(original,LV_ALIGN_TOP_MID,0,20);
    lv_anim_t unrelated;lv_anim_init(&unrelated);lv_anim_set_var(&unrelated,original);
    lv_anim_set_exec_cb(&unrelated,move_original);lv_anim_set_values(&unrelated,200,201);
    lv_anim_set_duration(&unrelated,600);lv_anim_set_repeat_count(&unrelated,LV_ANIM_REPEAT_INFINITE);
    assert(lv_anim_start(&unrelated));
    lv_obj_t *active=lv_screen_active();uint32_t children=lv_obj_get_child_count(lv_layer_top());
    uint16_t animations=lv_anim_count_running();
    lv_obj_t *boot=identity_boot_create(lv_layer_top());assert(boot);
    assert(lv_anim_count_running()==animations+1 && lv_obj_has_flag(boot,LV_OBJ_FLAG_CLICKABLE));
    assert(!lv_obj_has_flag(boot,LV_OBJ_FLAG_GESTURE_BUBBLE|LV_OBJ_FLAG_EVENT_BUBBLE));
    draw();assert(white_count()==0);export(folder,"boot-native-0");
    advance(200);int early=white_count();assert(early>0);export(folder,"boot-native-200");
    advance(400);assert(white_count()>early);export(folder,"boot-native-600");
    advance(400);check_dot(278.5,187.5);
    for(int y=0;y<160;++y)assert(memcmp(reference+y*160,pixels+(153+y)*W+153,160*sizeof(uint16_t))==0);
    advance(760);export(folder,"boot-native-final");
    advance(80);assert(lv_obj_get_child_count(lv_layer_top())==children);
    assert(lv_anim_count_running()==animations && lv_screen_active()==active && lv_obj_is_valid(original));
    // Early dismissal and repeated startup previews must leave no object or animation behind.
    for(int i=0;i<12;++i){boot=identity_boot_create(lv_layer_top());assert(boot);advance(80);lv_obj_delete(boot);advance(2000);
        assert(lv_anim_count_running()==animations && lv_obj_get_child_count(lv_layer_top())==children);}
    if(folder) {
        // Optional review export runs the same native scheduler, not the browser mockup.
        boot=identity_boot_create(lv_layer_top());assert(boot);int previous=0;
        for(int i=0;i<54;++i){int at=i*1000/30;advance(at-previous);previous=at;
            char name[40];snprintf(name,sizeof name,"boot-frame-%03d",i);export(folder,name);}
        advance(100);assert(lv_obj_get_child_count(lv_layer_top())==children);
    }
    lv_obj_delete(original);assert(lv_anim_count_running()==0);lv_deinit();
    puts("Native geometry/hole/gap, progressive ring, final mark equivalence, original page, timed and early cleanup passed");
    return 0;
}
