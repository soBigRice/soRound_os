#include "mimi_render.h"
#include "app.h"
#include "src/draw/lv_draw_private.h"
#include "src/draw/sw/lv_draw_sw.h"
#include "src/draw/sw/blend/lv_draw_sw_blend_private.h"
#include "src/draw/sw/blend/lv_draw_sw_blend_to_rgb565.h"
#include "src/draw/sw/blend/lv_draw_sw_blend_to_rgb565_swapped.h"
// 9.6由lvgl.h公开换序接口并映射原函数名；9.5仍需旧头，保留严格编译检查。
// 9.6 exposes byte swapping and the old-name mapping via lvgl.h;9.5 still needs this header, with strict warnings retained.
#if !LV_VERSION_CHECK(9, 6, 0)
#include "src/draw/sw/lv_draw_sw_utils.h"
#endif
#include <string.h>
#define SIDE MIMI_ASSET_SIDE
struct mimi_render {
 uint16_t fallback[SIDE*SIDE];
 const mimi_asset_frame_t *frame;
 struct {lv_image_dsc_t image;int x,y;} eye[2];
 const void *image_source;lv_color_format_t format;lv_draw_sw_blend_handler_t previous;bool attached;
};
static mimi_render_t *active;
// 一个软件绘制单元串行复用内部行；覆盖完整Comet的最大623条混合操作。
// A single software draw unit serializes internal rows, including the full Comet maximum of623blend operations.
_Alignas(16) static mimi_asset_span_t hot_spans[MIMI_ASSET_MAX_ROW_SPANS];
_Alignas(16) static uint8_t hot_rle[MIMI_ASSET_MAX_BASE_ROW_BYTES];
_Alignas(16) static uint16_t hot_pixels[SIDE];
// RGB565原生逐次舍入，q来自离线捕获的实际混合透明度；不合并重叠操作。
// Native per-operation RGB565 rounding; q comes from captured effective opacity, without merging overlaps.
// Packed-channel formula follows LVGL lv_color_16_16_mix (MIT; bundled license retained with pilot).
static inline uint16_t mix565(uint16_t fg,uint16_t bg,uint8_t q) {
 if(q==32)return fg;
 if(!q||fg==bg)return bg;
 uint32_t f=(fg|((uint32_t)fg<<16))&0x7e0f81f,b=(bg|((uint32_t)bg<<16))&0x7e0f81f;
 uint32_t v=(((((f-b)*q)>>5)+b)&0x7e0f81f);return (uint16_t)(v|(v>>16));
}
// 长度、输出数量及输入消耗均有边界；不接受部分解码或额外字节。
// Bound input, output and packet lengths; reject partial decoding and trailing bytes.
static bool decode(const uint8_t *in,size_t bytes,void *out,size_t units,unsigned unit) {
 uint8_t *dest=out;size_t at=0,done=0;
 while(at<bytes&&done<units){
  if(bytes-at<2)return false;
  uint16_t token=(uint16_t)(in[at]|in[at+1]<<8);at+=2;
  size_t count=token&0x7fff;if(!count||count>units-done)return false;
  size_t n=(token&0x8000)?unit:count*unit;if(n>bytes-at)return false;
  if(token&0x8000){if(unit==1)memset(dest+done,in[at],count);else{uint16_t value=(uint16_t)(in[at]|in[at+1]<<8);uint16_t *d=(uint16_t*)dest+done;for(size_t i=0;i<count;i++)d[i]=value;}}
  else memcpy(dest+done*unit,in+at,n);
  at+=n;done+=count;
 }
 return at==bytes&&done==units;
}
size_t mimi_render_bytes(void){return sizeof(mimi_render_t);}
mimi_render_t *mimi_render_init(void *memory){if(!memory)return NULL;mimi_render_t*p=memory;memset(p,0,sizeof *p);return p;}
const uint16_t *mimi_render_pixels(const mimi_render_t*p){return p->fallback;}
bool mimi_render_select(mimi_render_t*p,const mimi_asset_frame_t*f,const mimi_prepared_eyes_t*eyes){
 if(!p||!f||!eyes)return false;
 p->frame=f;
 for(unsigned e=0;e<2;e++){
  const mimi_prepared_eye_bounds_t*b=&eyes->eye[e];unsigned offset=b->top*208+b->left;
  p->eye[e].image=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_A8,.w=b->width,.h=b->height,.stride=208},.data=eyes->alpha[e]+offset,.data_size=208*208-offset};
  p->eye[e].x=b->x;p->eye[e].y=b->y;
 }
 return true;
}
void mimi_render_frame(mimi_render_t *p,uint16_t *target,int stride,lv_area_t clip,bool swapped){
 int width=lv_area_get_width(&clip);
 for(int y=clip.y1;y<=clip.y2;y++){
  uint16_t *row=target+(y-clip.y1)*stride;
  const mimi_asset_frame_t*f=p->frame;size_t base_bytes=f->base_rows[y+1]-f->base_rows[y];
  memcpy(hot_rle,f->base_rle+f->base_rows[y],base_bytes);
  uint16_t*dest=clip.x1==0&&width==SIDE?row:hot_pixels;
  if(!decode(hot_rle,base_bytes,dest,SIDE,2))return;
  if(dest!=row)memcpy(row,hot_pixels+clip.x1,(size_t)width*2);
  for(unsigned e=0;e<2;e++){
   const lv_image_dsc_t*i=&p->eye[e].image;if(!i->data)continue;int yy=y-p->eye[e].y;
   if(yy<0||yy>=(int)i->header.h)continue;
   int left=LV_MAX(clip.x1,p->eye[e].x),right=LV_MIN(clip.x2+1,p->eye[e].x+(int)i->header.w);
   const uint8_t*mask=i->data+yy*i->header.stride;
   uint16_t color=lv_color_to_u16(lv_color_hex(COL_BG));
   for(int x=left;x<right;x++){uint8_t a=mask[x-p->eye[e].x];row[x-clip.x1]=mix565(color,row[x-clip.x1],(uint8_t)((a+4u)>>3));}
  }
  unsigned span_count=f->span_rows[y+1]-f->span_rows[y];
  const uint8_t*source=f->span_data+(size_t)f->span_rows[y]*sizeof(mimi_asset_span_t);
  memcpy(hot_spans,source,(size_t)span_count*sizeof(mimi_asset_span_t));
  for(unsigned i=0;i<span_count;i++){
   const mimi_asset_span_t*s=hot_spans+i;int l=LV_MAX(clip.x1,s->x),r=LV_MIN(clip.x2+1,s->x+s->n);
   for(int x=l;x<r;x++)row[x-clip.x1]=mix565(s->color,row[x-clip.x1],s->q);
  }
  if(swapped)lv_draw_sw_rgb565_swap(row,(uint32_t)width);
 }
}
static void native_blend(lv_draw_task_t *task,const lv_draw_sw_blend_dsc_t *d) {
    mimi_render_t *p=active;
    if(p->previous) { p->previous(task,d);return; }
    lv_area_t a={LV_MAX(task->clip_area.x1,d->blend_area->x1),LV_MAX(task->clip_area.y1,d->blend_area->y1),
        LV_MIN(task->clip_area.x2,d->blend_area->x2),LV_MIN(task->clip_area.y2,d->blend_area->y2)};
    if(d->src_buf) {
        a.x1=LV_MAX(a.x1,d->src_area->x1);a.x2=LV_MIN(a.x2,d->src_area->x2);
        a.y1=LV_MAX(a.y1,d->src_area->y1);a.y2=LV_MIN(a.y2,d->src_area->y2);
        if(d->mask_area) { a.x1=LV_MAX(a.x1,d->mask_area->x1);a.x2=LV_MIN(a.x2,d->mask_area->x2);
            a.y1=LV_MAX(a.y1,d->mask_area->y1);a.y2=LV_MIN(a.y2,d->mask_area->y2); }
    }
    if(a.x1>a.x2||a.y1>a.y2)return;
    lv_layer_t *layer=task->target_layer;bool swapped=layer->color_format==LV_COLOR_FORMAT_RGB565_SWAPPED;
    void *dest=lv_draw_layer_go_to_xy(layer,a.x1-layer->buf_area.x1,a.y1-layer->buf_area.y1);
    const uint8_t *mask=NULL;int mask_stride=0;
    if(d->mask_buf&&d->mask_res!=LV_DRAW_SW_MASK_RES_FULL_COVER) {
        mask_stride=d->mask_stride?d->mask_stride:lv_area_get_width(d->mask_area);
        mask=d->mask_buf+(a.y1-d->mask_area->y1)*mask_stride+a.x1-d->mask_area->x1;
    }
    lv_area_t relative={a.x1-layer->buf_area.x1,a.y1-layer->buf_area.y1,a.x2-layer->buf_area.x1,a.y2-layer->buf_area.y1};
    if(!d->src_buf) {
        lv_draw_sw_blend_fill_dsc_t f={.dest_buf=dest,.dest_w=lv_area_get_width(&a),.dest_h=lv_area_get_height(&a),
            .dest_stride=(int32_t)layer->draw_buf->header.stride,.mask_buf=mask,.mask_stride=mask_stride,.color=d->color,.opa=d->opa,.relative_area=relative};
        if(swapped)lv_draw_sw_blend_color_to_rgb565_swapped(&f);else lv_draw_sw_blend_color_to_rgb565(&f);
    } else {
        lv_area_t source=*d->src_area;source.x1-=layer->buf_area.x1;source.x2-=layer->buf_area.x1;
        source.y1-=layer->buf_area.y1;source.y2-=layer->buf_area.y1;
        const uint8_t *src=(const uint8_t *)d->src_buf+(a.y1-d->src_area->y1)*d->src_stride+
            (((a.x1-d->src_area->x1)*lv_color_format_get_bpp(d->src_color_format))>>3);
        lv_draw_sw_blend_image_dsc_t f={.dest_buf=dest,.dest_w=lv_area_get_width(&a),.dest_h=lv_area_get_height(&a),
            .dest_stride=(int32_t)layer->draw_buf->header.stride,.mask_buf=mask,.mask_stride=mask_stride,.src_buf=src,
            .src_stride=(int32_t)d->src_stride,.src_color_format=d->src_color_format,.opa=d->opa,.blend_mode=d->blend_mode,.relative_area=relative,.src_area=source};
        if(swapped)lv_draw_sw_blend_image_to_rgb565_swapped(&f);else lv_draw_sw_blend_image_to_rgb565(&f);
    }
}
static void image_handler(lv_draw_task_t *task,const lv_draw_sw_blend_dsc_t *d){
 mimi_render_t*p=active;const lv_draw_image_dsc_t*i=task->type==LV_DRAW_TASK_TYPE_IMAGE?task->draw_dsc:NULL;
 if(!i||i->src!=p->image_source||!d->src_buf||!p->frame){native_blend(task,d);return;}
 if(d->opa<LV_OPA_MAX||d->mask_buf||d->blend_mode!=LV_BLEND_MODE_NORMAL){
  mimi_render_frame(p,p->fallback,SIDE,(lv_area_t){0,0,SIDE-1,SIDE-1},false);native_blend(task,d);return;
 }
 lv_layer_t*l=task->target_layer;
 lv_area_t a={LV_MAX(task->clip_area.x1,d->blend_area->x1),LV_MAX(task->clip_area.y1,d->blend_area->y1),LV_MIN(task->clip_area.x2,d->blend_area->x2),LV_MIN(task->clip_area.y2,d->blend_area->y2)};
 if(a.x1>a.x2||a.y1>a.y2)return;
 uint16_t*dest=lv_draw_layer_go_to_xy(l,a.x1-l->buf_area.x1,a.y1-l->buf_area.y1);
 lv_area_t c={a.x1-d->src_area->x1,a.y1-d->src_area->y1,a.x2-d->src_area->x1,a.y2-d->src_area->y1};
 mimi_render_frame(p,dest,(int)l->draw_buf->header.stride/2,c,l->color_format==LV_COLOR_FORMAT_RGB565_SWAPPED);
}
bool mimi_render_attach(mimi_render_t*p,const void*image){
 if(p->attached){if(p->previous){lv_draw_sw_custom_blend_handler_t h={p->format,p->previous};lv_draw_sw_register_blend_handler(&h);}else lv_draw_sw_unregister_blend_handler(p->format);p->attached=false;active=NULL;}
 p->image_source=image;if(!image)return true;
 p->format=lv_display_get_color_format(lv_display_get_default());p->previous=lv_draw_sw_get_blend_handler(p->format);
 lv_draw_sw_custom_blend_handler_t h={p->format,image_handler};p->attached=lv_draw_sw_register_blend_handler(&h);if(p->attached)active=p;return p->attached;
}
