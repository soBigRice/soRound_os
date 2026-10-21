#include <assert.h>
#include <stdio.h>
#include "render_power.h"
static render_power_t power;
_Alignas(LV_DRAW_BUF_ALIGN) static uint16_t buffers[2][466*40];
static unsigned draws,flushes;
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *pixels){
    (void)area;(void)pixels;assert(power.held && power.lock->held==1);++flushes;lv_display_flush_ready(display);
}
static void draw(lv_event_t *event){(void)event;assert(power.held && power.lock->held==1);++draws;}
int main(void){
    lv_init();lv_display_t *display=lv_display_create(466,466);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_buffers(display,buffers[0],buffers[1],sizeof buffers[0],LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);assert(render_power_bind(display,&power));
    lv_obj_t *view=lv_obj_create(lv_screen_active());lv_obj_set_size(view,466,466);
    lv_obj_add_event_cb(view,draw,LV_EVENT_DRAW_MAIN,NULL);lv_refr_now(display);
    assert(draws>1 && flushes>1 && !power.held && !power.lock->held);
    unsigned before=flushes;for(int i=0;i<100;i++){lv_tick_inc(20);lv_refr_now(display);assert(!power.held);}
    assert(flushes==before);
    lv_obj_invalidate(view);lv_refr_now(display);assert(flushes>before && !power.held);
    lv_display_delete(display);assert(!power.lock && !power.held);lv_deinit();
    puts("Render CPU lock covers every real tile, releases between frames, stays idle without dirty areas, and deletes cleanly.");
}
