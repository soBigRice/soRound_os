#include "native_ui.h"
#include "esp_heap_caps.h"
#include "src/draw/lv_draw_private.h"
#include "src/draw/sw/lv_draw_sw.h"
#include "src/draw/sw/blend/lv_draw_sw_blend_private.h"
#include <stdlib.h>
#include "front_canonical.h"

void go_home(void) {}
void launcher_set_app_fullscreen(bool enabled) {
    for(unsigned i=0;i<lv_obj_get_child_count(lv_layer_top());++i)
        ui_obj_set_hidden(lv_obj_get_child(lv_layer_top(),i),enabled);
}
static void *test_allocate(size_t bytes,unsigned caps) {
    assert(bytes==308840&&caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    void *memory=malloc(bytes);assert(memory);return memory;
}
#define heap_caps_malloc test_allocate
#define heap_caps_free free
#define MIMI_REFERENCE_ONLY 1
#include "../../main/app_mimi.c"
#undef heap_caps_malloc
#undef heap_caps_free

typedef mimi_asset_span_t span_t;
typedef struct { span_t *items;size_t count,capacity; } row_t;
static row_t rows[466];
static size_t native_calls;
static lv_draw_sw_custom_blend_handler_t capture_registration;

static void append(unsigned y,span_t s) {
    row_t *row=&rows[y];
    if(row->count==row->capacity) {
        row->capacity=row->capacity?row->capacity*2:128;
        row->items=realloc(row->items,row->capacity*sizeof s);assert(row->items);
    }
    row->items[row->count++]=s;
}

// 在原生565混合入口捕获有效透明度，保留每个原生调用的舍入顺序。
// Capture effective opacity at the native565 blend entry, preserving each call's rounding order.
static void capture_blend(lv_draw_task_t *task,const lv_draw_sw_blend_dsc_t *d) {
    assert(!d->src_buf);++native_calls;
    lv_area_t a={LV_MAX(task->clip_area.x1,d->blend_area->x1),LV_MAX(task->clip_area.y1,d->blend_area->y1),
        LV_MIN(task->clip_area.x2,d->blend_area->x2),LV_MIN(task->clip_area.y2,d->blend_area->y2)};
    bool masked=d->mask_buf&&d->mask_res!=LV_DRAW_SW_MASK_RES_FULL_COVER;
    int stride=masked?(d->mask_stride?d->mask_stride:lv_area_get_width(d->mask_area)):0;
    uint16_t color=lv_color_to_u16(d->color);
    for(int y=a.y1;y<=a.y2;++y) {
        assert(y>=0&&y<466);
        int run_x=-1,run_n=0;unsigned last_q=0;
        for(int x=a.x1;x<=a.x2+1;++x) {
            unsigned q=0;
            if(x<=a.x2) {
                assert(x>=0&&x<466);
                unsigned alpha;
                if(!masked) alpha=d->opa>=253?255:d->opa;
                else {
                    unsigned mask=d->mask_buf[(y-d->mask_area->y1)*stride+x-d->mask_area->x1];
                    alpha=d->opa>=253?mask:(mask*d->opa)>>8;
                }
                q=(alpha+4)>>3;
            }
            if(run_n&&q!=last_q) {
                append((unsigned)y,(span_t){(uint16_t)run_x,(uint16_t)run_n,color,(uint8_t)last_q,0});
                run_n=0;
            }
            if(q) { if(!run_n)run_x=x;last_q=q;++run_n; }
        }
    }
    // 同时走原生混合，以独立验证捕获后的span重放。
    // Also execute native blending so captured-span replay has an independent reference.
    lv_draw_sw_unregister_blend_handler(LV_COLOR_FORMAT_RGB565);
    lv_draw_sw_blend(task,d);
    assert(lv_draw_sw_register_blend_handler(&capture_registration));
}

static uint16_t mix_q(uint16_t fg,uint16_t bg,unsigned q) {
    if(q==32)return fg;
    if(!q||fg==bg)return bg;
    uint32_t b=(bg|((uint32_t)bg<<16))&0x7e0f81f;
    uint32_t f=(fg|((uint32_t)fg<<16))&0x7e0f81f;
    uint32_t r=((((f-b)*q)>>5)+b)&0x7e0f81f;
    return (uint16_t)(r>>16)|r;
}
static void direct_dot(lv_draw_task_t *task,float x,float y,float radius,uint32_t color,float alpha) {
    if(radius<.5f||alpha<=0)return;
    int size=(int)lroundf(radius*2);
    lv_draw_fill_dsc_t d;lv_draw_fill_dsc_init(&d);
    d.radius=LV_RADIUS_CIRCLE;d.color=lv_color_hex(color);d.opa=(lv_opa_t)lroundf(clamp(alpha,0,1)*255);
    lv_area_t a={(int)lroundf(x)-size/2,(int)lroundf(y)-size/2,0,0};
    a.x2=a.x1+size-1;a.y2=a.y1+size-1;lv_draw_sw_fill(task,&d,&a);
}
static void direct_front(uint16_t *destination) {
    lv_area_t full={0,0,465,465};lv_draw_buf_t draw;
    assert(lv_draw_buf_init(&draw,466,466,LV_COLOR_FORMAT_RGB565,932,destination,466*466*2)==LV_RESULT_OK);
    lv_layer_t layer={.draw_buf=&draw,.color_format=LV_COLOR_FORMAT_RGB565,.buf_area=full,.phy_clip_area=full,._clip_area=full};
    lv_draw_task_t task={.target_layer=&layer,.clip_area=full,.area=full};
    capture_registration=(lv_draw_sw_custom_blend_handler_t){LV_COLOR_FORMAT_RGB565,capture_blend};
    assert(!lv_draw_sw_get_blend_handler(LV_COLOR_FORMAT_RGB565));
    assert(lv_draw_sw_register_blend_handler(&capture_registration));
    for(unsigned arc=0;arc<decor_cache->count;++arc) {
        if(scene.arc_opa[arc]*motion_weight<.01f)continue;
        lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);d.round_start=d.round_end=true;
        d.width=decor_cache->width[arc];d.opa=decor_cache->opa[arc];
        for(unsigned i=1;i<=64;++i) {
            if(decor_cache->behind[arc][i]||decor_cache->behind[arc][i-1])continue;
            if(decor_cache->covered[arc][(i-1)/32]&(1u<<((i-1)%32)))continue;
            d.p1=decor_cache->points[arc][i-1];d.p2=decor_cache->points[arc][i];d.color=lv_color_hex(decor_cache->colors[arc][i]);
            lv_draw_sw_line(&task,&d);
        }
    }
    lv_area_t bounds;lv_obj_get_coords(root,&bounds);
    float ox=bounds.x1+232.5f,oy=bounds.y1+232.5f;
    if(!scene.dots_behind)for(unsigned i=0;i<5;++i)
        direct_dot(&task,ox+scene.dots[i][0]*motion_radius,oy+scene.dots[i][1]*motion_radius,
                   scene.dots[i][2]*motion_radius,TOOLS_WHITE,scene.dot_opa[i]*motion_weight);
    if(scene.notch[2]>0) {
        float x=ox+scene.notch[0]*motion_radius,y=oy+scene.notch[1]*motion_radius;
        direct_dot(&task,x,y,scene.notch[3]*motion_radius,COL_BG,motion_weight);
        direct_dot(&task,x,y,scene.notch[2]*motion_radius,0x2496e8,motion_weight);
    }
    lv_draw_sw_unregister_blend_handler(LV_COLOR_FORMAT_RGB565);
}
static void write_file(const char *dir,const char *name,unsigned frame,const void *data,size_t size) {
    char path[1024];snprintf(path,sizeof path,"%s/%s-%02u.bin",dir,name,frame);
    FILE *f=fopen(path,"wb");assert(f);assert(!size||fwrite(data,1,size,f)==size);assert(!fclose(f));
}
static void full_refresh(void) { lv_obj_invalidate(lv_screen_active());lv_refr_now(display); }
int main(int argc,char **argv) {
    assert(argc==2);const char *out=argv[1];
    init();lv_obj_t *parent=page("Mimi");app_mimi.enter(parent);full_refresh();
    uint16_t *reference=malloc(sizeof pixels),*base=malloc(sizeof pixels),*live=malloc(sizeof pixels),*native=malloc(sizeof pixels),*background=malloc(sizeof pixels);assert(reference&&base&&live&&native&&background);
    const unsigned clips[]={10,11,13};unsigned frame=0;
    for(unsigned clip_index=0;clip_index<3;++clip_index)for(unsigned ms=0;ms<=mimi_motion_duration(clips[clip_index])+450;ms+=50,++frame) {
        for(unsigned y=0;y<466;++y)rows[y].count=0;
        native_calls=0;
        mimi_pet_init(&pet);mimi_pet_play(&pet,clips[clip_index]);pet.motion_ms=ms;render_pet();
        for(unsigned eye=0;eye<2;++eye)ui_obj_set_hidden(eyes[eye],false);
        ui_obj_set_hidden(decor[1],false);full_refresh();memcpy(reference,pixels,sizeof pixels);
        ui_obj_set_hidden(decor[1],true);full_refresh();memcpy(live,pixels,sizeof pixels);memcpy(background,live,sizeof pixels);
        for(unsigned eye=0;eye<2;++eye)ui_obj_set_hidden(eyes[eye],true);
        full_refresh();memcpy(base,pixels,sizeof pixels);
        memcpy(native,live,sizeof pixels);direct_front(native);
        if(memcmp(native,reference,sizeof pixels)) {
            unsigned different=0;for(unsigned i=0;i<466*466;++i)if(native[i]!=reference[i]){if(different<8)fprintf(stderr,"front mismatch frame%u (%u,%u) %x %x\n",frame,i%466,i/466,native[i],reference[i]);++different;}
            fprintf(stderr,"direct-front mismatch %u pixels\n",different);return 2;
        }
        size_t total=0;uint32_t offsets[467]={0};
        for(unsigned y=0;y<466;++y) {
            offsets[y]=(uint32_t)total;total+=rows[y].count;
            for(size_t op=0;op<rows[y].count;++op) {
                span_t s=rows[y].items[op];
                for(unsigned x=s.x;x<s.x+s.n;++x)live[y*466+x]=mix_q(s.color,live[y*466+x],s.q);
            }
        }
        offsets[466]=(uint32_t)total;assert(!memcmp(live,reference,sizeof pixels));
        span_t *spans=malloc(total?total*sizeof *spans:1);assert(spans);size_t used=0;
        for(unsigned y=0;y<466;++y){memcpy(spans+used,rows[y].items,rows[y].count*sizeof *spans);used+=rows[y].count;}
        mimi_front_canonical_t canonical;
        assert(mimi_front_canonicalize(spans,offsets,frame,background,reference,mix_q,&canonical));
        write_file(out,"base",frame,base,sizeof pixels);write_file(out,"body",frame,masks,BODY_BYTES);
        write_file(out,"masks",frame,masks,PIXEL_BYTES);write_file(out,"reference",frame,reference,sizeof pixels);
        write_file(out,"spans",frame,spans,total*sizeof *spans);write_file(out,"rows",frame,offsets,sizeof offsets);
        write_file(out,"canonical-spans",frame,canonical.spans,canonical.count*sizeof *canonical.spans);
        write_file(out,"canonical-rows",frame,canonical.rows,sizeof canonical.rows);
        int32_t eye_info[2][7];
        for(unsigned eye=0;eye<2;++eye){lv_area_t a;lv_obj_get_coords(eyes[eye],&a);eye_info[eye][0]=a.x1;eye_info[eye][1]=a.y1;eye_info[eye][2]=images[eye+1].header.w;eye_info[eye][3]=images[eye+1].header.h;eye_info[eye][4]=images[eye+1].header.stride;eye_info[eye][5]=(int32_t)(images[eye+1].data-masks);eye_info[eye][6]=(int32_t)images[eye+1].data_size;}
        write_file(out,"eyes",frame,eye_info,sizeof eye_info);
        printf("frame=%u clip=%u duration=%u ms=%u native_calls=%zu spans=%zu bytes=%zu exact=true canonical_spans=%zu canonical_max_row=%u canonical_opaque_pixels=%llu canonical_transparent_pixels=%llu old_pixel_ops=%llu canonical_pixel_ops=%llu canonical_background_checks=%llu canonical_exhaustive_checks=%llu canonical_unchanged_sequences=%llu canonical_sample_depth=%u canonical_exact=true\n",
            frame,clips[clip_index],mimi_motion_duration(clips[clip_index]),ms,native_calls,total,total*sizeof *spans,
            canonical.count,canonical.max_row,(unsigned long long)canonical.opaque_pixels,(unsigned long long)canonical.transparent_pixels,
            (unsigned long long)canonical.old_pixel_ops,(unsigned long long)canonical.pixel_ops,(unsigned long long)canonical.background_checks,
            (unsigned long long)canonical.exhaustive_background_checks,(unsigned long long)canonical.unchanged_sequences,canonical.sample_depth);
        fflush(stdout);free(spans);mimi_front_canonical_free(&canonical);assert(getchar()=='\n');
    }
    app_mimi.exit();lv_deinit();free(reference);free(base);free(live);free(native);free(background);
    for(unsigned y=0;y<466;++y)free(rows[y].items);
    return 0;
}
