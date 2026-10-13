#include "lvgl.h"
#include "weather_artwork.h"
// Observe the real decoder callback in tests; production uses only LVGL's
// supported cache configuration and public display APIs.
#if LV_VERSION_CHECK(9,6,0)
#include "src/image/lv_image_decoder_private.h"
#else
#include "src/draw/lv_image_decoder_private.h"
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { W=466, FRAMES=24 };
static uint8_t panel[W*W*2];
static uint8_t expected[FRAMES][W*W*2];
_Alignas(LV_DRAW_BUF_ALIGN) static uint8_t tile[W*40*2];
static unsigned opens;
static lv_image_decoder_open_f_t original_open;
static lv_result_t counted_open(lv_image_decoder_t *decoder,lv_image_decoder_dsc_t *dsc){
    ++opens;return original_open(decoder,dsc);
}
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *pixels){
    size_t width=(size_t)lv_area_get_width(area)*2;
    for(int y=area->y1;y<=area->y2;++y)
        memcpy(panel+((size_t)y*W+area->x1)*2,pixels+(size_t)(y-area->y1)*width,width);
    lv_display_flush_ready(display);
}
int main(void){
    lv_init();
    const weather_artwork_t *art=weather_artwork_for(0,true);assert(art);
    lv_image_cache_resize(0,true);
    lv_image_decoder_dsc_t probe;
    assert(lv_image_decoder_open(&probe,art->image,NULL)==LV_RESULT_OK);
    lv_image_decoder_t *decoder=probe.decoder;assert(decoder&&decoder->open_cb);
    lv_image_decoder_close(&probe);
    original_open=decoder->open_cb;decoder->open_cb=counted_open;
    lv_display_t *display=lv_display_create(W,W);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,tile,NULL,sizeof tile,LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);
    lv_obj_t *screen=lv_screen_active();lv_obj_set_style_bg_color(screen,lv_color_hex(0),0);
    lv_obj_t *image=lv_image_create(screen);lv_image_set_src(image,art->image);
    unsigned uncached=0;
    for(unsigned phase=0;phase<2;++phase){
        lv_image_cache_resize(phase?1048576:0,true);opens=0;
        for(unsigned frame=0;frame<FRAMES;++frame){
            lv_obj_set_pos(image,80+(int)(frame%3),140+(int)(frame%5));
            lv_obj_invalidate(screen);lv_refr_now(display);
            if(!phase)memcpy(expected[frame],panel,sizeof panel);else assert(!memcmp(expected[frame],panel,sizeof panel));
        }
        if(!phase){uncached=opens;assert(uncached>=FRAMES);}
        else {assert(opens==1);printf("24 identical moving weather frames: decoder opens %u -> %u; pixels unchanged\n",uncached,opens);}
    }
    lv_image_cache_drop(NULL);decoder->open_cb=original_open;lv_deinit();return 0;
}
