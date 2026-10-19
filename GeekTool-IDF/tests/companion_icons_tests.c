#include "companion_icons.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint16_t framebuffer[466*466], reference[466*466], buffer[466*40];
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *data) {
    unsigned width=(unsigned)lv_area_get_width(a);
    for(int y=a->y1;y<=a->y2;y++) {memcpy(framebuffer+y*466+a->x1,data,width*2);data+=width*2;}
    lv_display_flush_ready(d);
}
static void render(lv_display_t *display,int index,bool adapter,uint8_t opacity) {
    lv_obj_clean(lv_screen_active()); memset(framebuffer,0,sizeof framebuffer);
    lv_obj_t *parent=lv_obj_create(lv_screen_active()); lv_obj_remove_style_all(parent);
    lv_obj_set_pos(parent,122,166);lv_obj_set_size(parent,148,148);
    lv_obj_set_style_opa_layered(parent,opacity,0);
    lv_obj_t *icon=adapter?companion_launcher_icon_create(parent,index):launcher_icon_create(parent,(launcher_icon_t)index);
    assert(icon);lv_obj_center(icon);lv_obj_update_layout(lv_screen_active());
    lv_obj_invalidate(lv_screen_active());lv_refr_now(display);
}
int main(void) {
    lv_init();lv_display_t *display=lv_display_create(466,466);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,buffer,NULL,sizeof buffer,LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);lv_obj_set_style_bg_color(lv_screen_active(),lv_color_black(),0);
    // 复用适配层不能改变旧图标像素、平移坐标或父层透明度。
    // The adapter must preserve original pixels, translation and parent opacity.
    for(int i=0;i<LAUNCHER_ICON_COUNT;i++) for(unsigned alpha=127;alpha<=255;alpha+=128) {
        render(display,i,false,(uint8_t)alpha);memcpy(reference,framebuffer,sizeof reference);
        render(display,i,true,(uint8_t)alpha);assert(!memcmp(reference,framebuffer,sizeof reference));
    }
    for(int i=0;i<COMPANION_ICON_COUNT;i++) {
        render(display,LAUNCHER_ICON_COUNT+i,true,255);
        unsigned ink=0;for(unsigned k=0;k<466*466;k++)ink+=framebuffer[k]!=0;
        assert(ink>100);
    }
    lv_deinit();puts("companion icons: shipped catalog pixels/fade preserved; all new icons render");
}
