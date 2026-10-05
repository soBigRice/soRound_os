#include "weather_frame.h"
#include <string.h>
static bool valid(weather_frame_area_t a) {
    return a.x1>=0&&a.y1>=0&&a.x2>=a.x1&&a.y2>=a.y1&&a.x2<WEATHER_FRAME_WIDTH&&a.y2<WEATHER_FRAME_HEIGHT;
}
bool weather_frame_patch(weather_frame_t *f,weather_frame_area_t a,const uint8_t *tile,size_t stride) {
    if(!f||!f->pixels||!tile||!valid(a))return false;
    size_t width=(size_t)(a.x2-a.x1+1)*2;if(stride<width)return false;
    for(int y=a.y1;y<=a.y2;++y)memcpy(f->pixels+((size_t)y*WEATHER_FRAME_WIDTH+a.x1)*2,tile+(size_t)(y-a.y1)*stride,width);
    if(!f->dirty)f->area=a;
    else {
        if(a.x1<f->area.x1)f->area.x1=a.x1;
        if(a.y1<f->area.y1)f->area.y1=a.y1;
        if(a.x2>f->area.x2)f->area.x2=a.x2;
        if(a.y2>f->area.y2)f->area.y2=a.y2;
    }
    f->dirty=true;return true;
}
bool weather_frame_read(const weather_frame_t *f,weather_frame_area_t a,uint8_t *tile,size_t capacity) {
    if(!f||!f->pixels||!tile||!valid(a))return false;
    size_t width=(size_t)(a.x2-a.x1+1)*2,bytes=width*(size_t)(a.y2-a.y1+1);if(bytes>capacity)return false;
    for(int y=a.y1;y<=a.y2;++y)memcpy(tile+(size_t)(y-a.y1)*width,f->pixels+((size_t)y*WEATHER_FRAME_WIDTH+a.x1)*2,width);
    return true;
}
