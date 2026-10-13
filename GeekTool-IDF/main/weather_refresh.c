#include "weather_refresh.h"
#include <stdlib.h>
bool weather_refresh_flush(weather_refresh_t *r,lv_display_t *disp,const lv_area_t *a,uint8_t *pixels) {
    if(!r->frame.pixels)return false;
    lv_draw_buf_t *draw=lv_display_get_buf_active(disp);
    if(!draw)abort();
    // Learn only public, currently active buffers; do not reach into the port's
    // private display context or allocate another scarce internal DMA buffer.
    if(pixels==draw->data) {
        for(unsigned i=0;i<2;++i) {
            if(r->staging[i].pixels==pixels || !r->staging[i].pixels) {
                r->staging[i].pixels=pixels;r->staging[i].capacity=draw->data_size;break;
            }
        }
    }
    weather_frame_area_t area={a->x1,a->y1,a->x2,a->y2};
    uint32_t stride=lv_draw_buf_width_to_stride(lv_area_get_width(a),LV_COLOR_FORMAT_RGB565_SWAPPED);
    if(!weather_frame_patch(&r->frame,area,pixels,stride))abort();
    if(!lv_display_flush_is_last(disp)){lv_display_flush_ready(disp);return true;}
    if(r->wait_te)r->wait_te(r->arg);
    // The final draw buffer is no longer needed for rendering. Retain the outstanding flush
    // until every DMA tile is sent, preventing either LVGL buffer from being reused mid-burst.
    uint8_t *buffers[2]={pixels,NULL};size_t capacity=draw->data_size;
    for(unsigned i=0;pixels==draw->data && i<2;++i)if(r->staging[i].pixels && r->staging[i].pixels!=pixels) {
        buffers[1]=r->staging[i].pixels;
        if(r->staging[i].capacity<capacity)capacity=r->staging[i].capacity;
        break;
    }
    weather_frame_area_t send=r->frame.area;int width=send.x2-send.x1+1;
    int rows=(int)(capacity/((size_t)width*2));rows &= ~1;if(rows<2)abort();
    unsigned index=0;bool pending=false;
    for(int y=send.y1;y<=send.y2;y+=rows) {
        uint8_t *next=buffers[index];
        // With one buffer, completion must precede copying. With two, prepare
        // the free buffer while DMA reads the other, then wait before submit.
        if(pending && !buffers[1]){r->wait_send(r->arg);pending=false;}
        area=send;area.y1=y;area.y2=y+rows-1;if(area.y2>send.y2)area.y2=send.y2;
        if(!weather_frame_read(&r->frame,area,next,capacity))abort();
        if(pending)r->wait_send(r->arg);
        r->send(r->arg,area,next);pending=r->wait_send!=NULL;
        if(buffers[1])index^=1;
    }
    if(pending)r->wait_send(r->arg);
    r->frame.dirty=false;lv_display_flush_ready(disp);return true;
}
