#pragma once
#include "watchface_ui.h"
#include "tools_ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// 新系列只共享绘制原语，旧21款的绘制路径保持原样。 / Share primitives only for STUDIO; leave the original 21 renderers intact.
typedef struct {lv_layer_t *layer;int x,y,size;bool preview;} wfs_draw_t;
typedef struct {float x,y;} wfs_point_t;
typedef struct {
    wfs_point_t iris[8][36],growth[12][121];
    uint16_t iris_ready,growth_ready;
} wfs_geometry_t;
wfs_geometry_t *wfs_geometry_cache(void);
void wfs_geometry_release(void);
typedef struct {
    float px[96],py[96],xs[96];
    uint16_t coverage[466];
} wfs_draw_scratch_t;
// 工作区只在唯一taskLVGL线程的非重入STUDIO绘制中借用；同步软件后端不会回调STUDIO。
// Borrow this workspace only during non-reentrant STUDIO drawing on taskLVGL; the synchronous software backend never calls STUDIO.
extern wfs_draw_scratch_t g_wfs_draw_scratch;
typedef struct {int16_t x1,x2,y1,y2;lv_opa_t opacity;} wfs_span_t;
extern wfs_span_t g_wfs_spans[4];
static inline void wfs_spans_begin(void){for(unsigned i=0;i<4;i++)g_wfs_spans[i].y2=-1;}
static inline void wfs_span_flush(const wfs_draw_t *d,lv_draw_rect_dsc_t *rect,wfs_span_t *span){
    if(span->y2<0)return;
    rect->bg_opa=span->opacity;lv_area_t area={d->x+span->x1,d->y+span->y1,d->x+span->x2,d->y+span->y2};lv_draw_rect(d->layer,rect,&area);span->y2=-1;
}
static inline void wfs_span(const wfs_draw_t *d,lv_draw_rect_dsc_t *rect,int x1,int x2,int y,lv_opa_t opacity){
    if(!opacity)return;
    unsigned oldest=0;
    for(unsigned i=0;i<4;i++){
        wfs_span_t *span=&g_wfs_spans[i];
        if(span->y2>=0 && span->y2==y-1 && span->x1==x1 && span->x2==x2 && span->opacity==opacity){span->y2=(int16_t)y;return;}
        if(span->y2<g_wfs_spans[oldest].y2)oldest=i;
    }
    // 同一原语内互不相交的等色行可合并为矩形；四个槽满时先提交最旧项，不改变叠层顺序。
    // Merge disjoint equal-color rows within one primitive; flush the oldest of four bounded slots without reordering primitives.
    wfs_span_t *span=&g_wfs_spans[oldest];wfs_span_flush(d,rect,span);
    *span=(wfs_span_t){(int16_t)x1,(int16_t)x2,(int16_t)y,(int16_t)y,opacity};
}
static inline void wfs_spans_end(const wfs_draw_t *d,lv_draw_rect_dsc_t *rect){for(unsigned i=0;i<4;i++)wfs_span_flush(d,rect,&g_wfs_spans[i]);}
#include "watchface_studio_fonts.h"
static inline void wfs_bar_opa(const wfs_draw_t*,float,float,float,float,float,uint32_t,float);
static inline void wfs_line_opa(const wfs_draw_t*,float,float,float,float,float,uint32_t,float);
static inline void wfs_ring_opa(const wfs_draw_t*,float,float,float,float,uint32_t,float,bool);
static inline void wfs_poly(const wfs_draw_t*,const wfs_point_t*,int,uint32_t);
static inline wfs_draw_t wfs_make(lv_layer_t *layer,const lv_area_t *area,bool preview){return (wfs_draw_t){layer,area->x1,area->y1,lv_area_get_width(area),preview};}
static inline int wfs_scaled(const wfs_draw_t *d,float value){return (int)lroundf(value*d->size/466.0f);}
static inline void wfs_disc(const wfs_draw_t *d,float x,float y,float radius,uint32_t color){wfs_ring_opa(d,x,y,radius,0,color,1,true);}
static inline void wfs_circle(const wfs_draw_t *d,float x,float y,float radius,float width,uint32_t color){wfs_ring_opa(d,x,y,radius,width,color,1,false);}
static inline bool wfs_clip_line(float *x1,float *y1,float *x2,float *y2,float width){
    float dx=*x2-*x1,dy=*y2-*y1,px=*x1-233,py=*y1-233,r=232-width*.5f;
    float a=dx*dx+dy*dy;if(a<.0001f)return px*px+py*py<=r*r;
    float b=2*(px*dx+py*dy),c=px*px+py*py-r*r,disc=b*b-4*a*c;if(disc<0)return false;
    float lo=fmaxf(0,(-b-sqrtf(disc))/(2*a)),hi=fminf(1,(-b+sqrtf(disc))/(2*a));if(lo>hi)return false;
    float x=*x1,y=*y1;*x1=x+dx*lo;*y1=y+dy*lo;*x2=x+dx*hi;*y2=y+dy*hi;return true;
}
static inline void wfs_line(const wfs_draw_t *d,float x1,float y1,float x2,float y2,float width,uint32_t color){wfs_line_opa(d,x1,y1,x2,y2,width,color,1);}
static inline void wfs_bar(const wfs_draw_t *d,float x1,float y1,float x2,float y2,float width,uint32_t color){wfs_bar_opa(d,x1,y1,x2,y2,width,color,1);}
static inline void wfs_rect(const wfs_draw_t *d,float x,float y,float width,float height,float radius,uint32_t color){
    lv_draw_rect_dsc_t rect;lv_draw_rect_dsc_init(&rect);rect.bg_color=lv_color_hex(color);rect.radius=wfs_scaled(d,radius);
    lv_area_t area={d->x+wfs_scaled(d,x),d->y+wfs_scaled(d,y),d->x+wfs_scaled(d,x+width)-1,d->y+wfs_scaled(d,y+height)-1};lv_draw_rect(d->layer,&rect,&area);
}
static inline void wfs_arc(const wfs_draw_t *d,float x,float y,float radius,float start,float end,float width,uint32_t color){
    if(end<=start)return;
    if(end-start>=360){wfs_circle(d,x,y,radius,width,color);return;}
    // SVG圆弧描边以路径为中心；避免LVGL整数角度和内侧描边改变迷宫半径。
    // Center strokes on the SVG path instead of quantizing LVGL angles and drawing inward.
    int steps=LV_CLAMP(1,(int)ceilf((end-start)/2),46),count=0;wfs_point_t points[96];
    for(int side=0;side<2;side++)for(int j=0;j<=steps;j++){
        int i=side?steps-j:j;float angle=(start+(end-start)*i/steps)*0.017453292519943295f,r=radius+(side?-.5f:.5f)*width;
        points[count++]=(wfs_point_t){x+cosf(angle)*r,y+sinf(angle)*r};
    }
    wfs_poly(d,points,count,color);
}
static inline const lv_font_t *wfs_font(const wfs_draw_t *d,int size,bool heavy){return wfs_studio_font(size,d->preview,heavy);}
static inline float wfs_glyph_advance(const lv_font_t *font,uint32_t code){
    const lv_font_fmt_txt_dsc_t *format=font->dsc;
    for(unsigned i=0;i<format->cmap_num;i++){const lv_font_fmt_txt_cmap_t *map=&format->cmaps[i];
        if(code<map->range_start||code>=map->range_start+map->range_length)continue;
        for(unsigned j=0;j<map->list_length;j++)if(map->unicode_list[j]==code-map->range_start)return format->glyph_dsc[map->glyph_id_start+j].adv_w/16.0f;
    }
    return 0;
}
static inline unsigned wfs_codepoint(const char *value,unsigned *bytes){
    const unsigned char *p=(const unsigned char *)value;*bytes=1;
    if(p[0]<128)return p[0];
    if((p[0]&0xe0)==0xc0&&p[1]){*bytes=2;return ((p[0]&31u)<<6)|(p[1]&63u);}
    return 0;
}
static inline void wfs_label_font(const wfs_draw_t *d,const char *value,float x,float baseline,const lv_font_t *font,uint32_t color,float spacing){
    // SVG折叠连续空格；保留字体原始1/16px字宽，避免逐字整数进位拉宽时间。 / Match SVG whitespace and retain 1/16px advances instead of rounding each character.
    char normalized[96];unsigned length=0;bool space=false;
    for(const char *p=value;*p&&length+1<sizeof normalized;p++){if(*p==' '&&space)continue;normalized[length++]=*p;space=*p==' ';}normalized[length]=0;
    float scale=d->size/466.0f,gap=spacing*scale,total=0;
    for(unsigned i=0;i<length;){unsigned bytes;unsigned code=wfs_codepoint(normalized+i,&bytes);total+=wfs_glyph_advance(font,code)+gap;i+=bytes;}
    float left=d->x+x*scale-total*.5f,advance=0;int top=d->y+wfs_scaled(d,baseline)-font->line_height+font->base_line;
    for(unsigned i=0;i<length;){unsigned bytes;unsigned code=wfs_codepoint(normalized+i,&bytes);char glyph[3]={0};memcpy(glyph,normalized+i,bytes);
        int px=(int)lroundf(left+advance);lv_font_glyph_dsc_t metrics={0};
        // 用真实字形墨迹边界裁剪，保留原始字距；块外字符不申请绘制任务或复制文字。
        // Cull by actual glyph ink bounds without changing advances, avoiding tasks/text copies outside this tile.
        bool visible=true;
        if(lv_font_get_glyph_dsc(font,&metrics,code,0)){
            int ink_x=px+metrics.ofs_x,ink_y=top+font->line_height-font->base_line-metrics.box_h-metrics.ofs_y;
            const lv_area_t *clip=&d->layer->_clip_area;
            visible=metrics.box_w && metrics.box_h && ink_x<=clip->x2 && ink_x+metrics.box_w-1>=clip->x1 && ink_y<=clip->y2 && ink_y+metrics.box_h-1>=clip->y1;
        }
        if(visible){
            lv_draw_label_dsc_t label;lv_draw_label_dsc_init(&label);label.font=font;label.text=glyph;label.text_local=1;label.color=lv_color_hex(color);
            lv_area_t area={px,top,px+font->line_height*2,top+font->line_height-1};lv_draw_label(d->layer,&label,&area);
        }
        advance+=wfs_glyph_advance(font,code)+gap;i+=bytes;
    }
}
// 显式字体入口复用同一字形定位；旧调用仍解析原字库，像素契约保持。
// Explicit fonts share glyph placement; existing callers still resolve their original fonts and pixel contract.
static inline void wfs_label(const wfs_draw_t *d,const char *value,float x,float baseline,int size,uint32_t color,float spacing,bool heavy){wfs_label_font(d,value,x,baseline,wfs_font(d,size,heavy),color,spacing);}
static inline void wfs_text(const wfs_draw_t *d,const char *value,float x,float baseline,int size,uint32_t color,float spacing){wfs_label(d,value,x,baseline,size,color,spacing,false);}
static inline void wfs_heavy_text(const wfs_draw_t *d,const char *value,float x,float baseline,int size,uint32_t color,float spacing){wfs_label(d,value,x,baseline,size,color,spacing,true);}
static inline void wfs_time(const watchface_data_t *data,char *buffer,size_t size){snprintf(buffer,size,"%02d:%02d",data->time.tm_hour,data->time.tm_min);}
static inline void wfs_date(const watchface_data_t *data,char *buffer,size_t size){
    static const char *const days[]={"SUN","MON","TUE","WED","THU","FRI","SAT"};
    snprintf(buffer,size,"%s %02d / %02d",days[LV_CLAMP(0,data->time.tm_wday,6)],data->time.tm_mon+1,data->time.tm_mday);
}
static inline void wfs_aod(const wfs_draw_t *d,const watchface_data_t *data,const char *name){
    static const char *const months[]={"JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"};
    char value[32];wfs_text(d,name,233,174,13,0x4f554b,3);wfs_time(data,value,sizeof value);wfs_text(d,value,233,265,86,0x9a9d91,0);
    wfs_line(d,183,294,283,294,1,0x373c33);snprintf(value,sizeof value,"%02d %s",data->time.tm_mday,months[LV_CLAMP(0,data->time.tm_mon,11)]);wfs_text(d,value,233,323,15,0x60685c,0);
}
static inline void wfs_poly_opa(const wfs_draw_t *d,const wfs_point_t *points,int count,uint32_t color,float opacity){
    // 仅466px正屏/233px预览：有界行缓冲和四次边缘采样，不申请全屏画布。 / Bound scratch space to the 466/233px surfaces; four edge samples, no full-frame canvas.
    if(count<3||count>96||d->size<1||d->size>466)return;
    float *px=g_wfs_draw_scratch.px,*py=g_wfs_draw_scratch.py;
    float top=466,bottom=0,min_x=466,max_x=0,scale=d->size/466.0f;
    for(int i=0;i<count;i++){px[i]=points[i].x*scale-.5f;py[i]=points[i].y*scale-.5f;top=fminf(top,py[i]);bottom=fmaxf(bottom,py[i]);min_x=fminf(min_x,px[i]);max_x=fmaxf(max_x,px[i]);}
    // 半像素覆盖率仍用四次采样；只清空/扫描图形与当前块相交的列。
    // Keep four half-pixel coverage samples; clear/scan only columns intersecting the shape and current tile.
    int left=LV_MAX(LV_MAX(0,d->layer->_clip_area.x1-d->x),(int)floorf(min_x)-1);
    int right=LV_MIN(LV_MIN(d->size-1,d->layer->_clip_area.x2-d->x),(int)ceilf(max_x)+1);
    if(left>right)return;
    float opacity_scale=fminf(1,fmaxf(0,opacity));
    wfs_spans_begin();
    lv_draw_rect_dsc_t rect;lv_draw_rect_dsc_init(&rect);rect.bg_color=lv_color_hex(color);
    float center=d->size*.5f-.5f,radius=232*scale;
    // 只扫描当前DMA分块可见行，像素和覆盖率不变。 / Rasterize only the current DMA tile without changing pixel coverage.
    int clip_top=LV_MAX(0,d->layer->_clip_area.y1-d->y),clip_bottom=LV_MIN(d->size-1,d->layer->_clip_area.y2-d->y);
    for(int y=LV_MAX(clip_top,(int)floorf(top));y<=LV_MIN(clip_bottom,(int)ceilf(bottom));y++){
        uint16_t *coverage=g_wfs_draw_scratch.coverage;memset(coverage+left,0,(size_t)(right-left+1)*sizeof *coverage);
        for(int sample=0;sample<4;sample++){
            float sy=y-.375f+sample*.25f,dy=sy-center;if(fabsf(dy)>=radius)continue;
            float reach=sqrtf(radius*radius-dy*dy),lo=center-reach,hi=center+reach;
            float *xs=g_wfs_draw_scratch.xs;int n=0;
            for(int i=0,j=count-1;i<count;j=i++)if((py[i]>sy)!=(py[j]>sy))xs[n++]=px[i]+(sy-py[i])*(px[j]-px[i])/(py[j]-py[i]);
            for(int i=1;i<n;i++){float x=xs[i];int j=i;while(j>0&&xs[j-1]>x){xs[j]=xs[j-1];j--;}xs[j]=x;}
            for(int i=0;i+1<n;i+=2){
                float a=fmaxf(lo,xs[i]),b=fminf(hi,xs[i+1]);if(a>=b)continue;
                int first=LV_MAX(left,(int)floorf(a+.5f)),last=LV_MIN(right,(int)floorf(b+.5f));
                for(int x=first;x<=last;x++){float amount=x==first||x==last?fmaxf(0,fminf(b,x+.5f)-fmaxf(a,x-.5f)):1;coverage[x]+=(uint16_t)lroundf(amount*255);}
            }
        }
        for(int x=left;x<=right;){unsigned alpha=(coverage[x]+2u)/4u;if(!alpha){x++;continue;}int end=x;while(end<right&&(coverage[end+1]+2u)/4u==alpha)end++;
            wfs_span(d,&rect,x,end,y,(lv_opa_t)lroundf(LV_MIN(255u,alpha)*opacity_scale));x=end+1;}
    }
    wfs_spans_end(d,&rect);
}

static inline void wfs_poly(const wfs_draw_t *d,const wfs_point_t *points,int count,uint32_t color){wfs_poly_opa(d,points,count,color,1);}
static inline void wfs_rect_opa(const wfs_draw_t *d,float x,float y,float width,float height,float radius,uint32_t color,float opacity){
    lv_draw_rect_dsc_t rect;lv_draw_rect_dsc_init(&rect);rect.bg_color=lv_color_hex(color);rect.bg_opa=(lv_opa_t)lroundf(fminf(1,fmaxf(0,opacity))*255);rect.radius=wfs_scaled(d,radius);
    lv_area_t area={d->x+wfs_scaled(d,x),d->y+wfs_scaled(d,y),d->x+wfs_scaled(d,x+width)-1,d->y+wfs_scaled(d,y+height)-1};lv_draw_rect(d->layer,&rect,&area);
}
static inline void wfs_bar_opa(const wfs_draw_t *d,float x1,float y1,float x2,float y2,float width,uint32_t color,float opacity){
    if(width<=0)return;
    float dx=x2-x1,dy=y2-y1,length=hypotf(dx,dy);if(length<.0001f)return;
    float nx=-dy/length*width*.5f,ny=dx/length*width*.5f;
    const wfs_point_t points[]={{x1+nx,y1+ny},{x2+nx,y2+ny},{x2-nx,y2-ny},{x1-nx,y1-ny}};
    wfs_poly_opa(d,points,4,color,opacity);
}
static inline void wfs_line_opa(const wfs_draw_t *d,float x1,float y1,float x2,float y2,float width,uint32_t color,float opacity){
    wfs_bar_opa(d,x1,y1,x2,y2,width,color,opacity);
    // 圆端只影响端点；亚像素描边主体交给采样多边形。 / Rounded caps affect endpoints only; sampled polygons retain fractional stroke widths.
    wfs_ring_opa(d,x1,y1,width*.5f,0,color,opacity,true);wfs_ring_opa(d,x2,y2,width*.5f,0,color,opacity,true);
}

static inline void wfs_cov_span(uint16_t *coverage,int left,int right,float a,float b){
    if(a>=b)return;
    int first=LV_MAX(left,(int)floorf(a)),last=LV_MIN(right,(int)floorf(b));
    for(int x=first;x<=last;x++){float amount=x==first||x==last?fmaxf(0,fminf(b,x+1.0f)-fmaxf(a,(float)x)):1;coverage[x]+=(uint16_t)lroundf(amount*255);}
}
static inline void wfs_ring_opa(const wfs_draw_t *d,float x,float y,float radius,float width,uint32_t color,float opacity,bool fill){
    if(d->size<1||d->size>466||radius<=0)return;
    // SVG坐标位于像素边界，亚像素细线/小圆按真实覆盖率栅格化。 / SVG coordinates lie on pixel edges; retain true coverage for fractional strokes and discs.
    float scale=d->size/466.0f,cx=x*scale,cy=y*scale,outer=(radius+(fill?0:width*.5f))*scale,inner=fill?0:fmaxf(0,(radius-width*.5f)*scale),clip=d->size*.5f,clip_r=232*scale;
    int left=LV_MAX(LV_MAX(0,d->layer->_clip_area.x1-d->x),(int)floorf(cx-outer)-1);
    int right=LV_MIN(LV_MIN(d->size-1,d->layer->_clip_area.x2-d->x),(int)ceilf(cx+outer)+1);
    if(left>right)return;
    float opacity_scale=fminf(1,fmaxf(0,opacity));
    wfs_spans_begin();
    lv_draw_rect_dsc_t rect;lv_draw_rect_dsc_init(&rect);rect.bg_color=lv_color_hex(color);
    int clip_top=LV_MAX(0,d->layer->_clip_area.y1-d->y),clip_bottom=LV_MIN(d->size-1,d->layer->_clip_area.y2-d->y);
    for(int row=LV_MAX(clip_top,(int)floorf(cy-outer));row<=LV_MIN(clip_bottom,(int)ceilf(cy+outer));row++){
        uint16_t *coverage=g_wfs_draw_scratch.coverage;memset(coverage+left,0,(size_t)(right-left+1)*sizeof *coverage);
        for(int sample=0;sample<4;sample++){
            float sy=row+.125f+sample*.25f,dy=sy-cy,clip_dy=sy-clip;if(fabsf(dy)>=outer||fabsf(clip_dy)>=clip_r)continue;
            float reach=sqrtf(outer*outer-dy*dy),clip_reach=sqrtf(clip_r*clip_r-clip_dy*clip_dy),lo=fmaxf(cx-reach,clip-clip_reach),hi=fminf(cx+reach,clip+clip_reach);
            if(inner>0&&fabsf(dy)<inner){float hole=sqrtf(inner*inner-dy*dy);wfs_cov_span(coverage,left,right,lo,fminf(hi,cx-hole));wfs_cov_span(coverage,left,right,fmaxf(lo,cx+hole),hi);}
            else wfs_cov_span(coverage,left,right,lo,hi);
        }
        for(int col=left;col<=right;){unsigned alpha=(coverage[col]+2u)/4u;if(!alpha){col++;continue;}int end=col;while(end<right&&(coverage[end+1]+2u)/4u==alpha)end++;
            wfs_span(d,&rect,col,end,row,(lv_opa_t)lroundf(LV_MIN(255u,alpha)*opacity_scale));col=end+1;}
    }
    wfs_spans_end(d,&rect);
}
