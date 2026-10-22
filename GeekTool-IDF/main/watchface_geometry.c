#include "watchface_geometry.h"
#include "watchface_studio_draw.h"

#define GEOMETRY_TAU 6.2831853071795864769f
#define PAPER 0xe9e6db
#define MINT 0x9ab8ac
#define GOLD 0xd5a36c
#define DIM 0x373e3c

typedef struct {float day,hour12,minute;} geometry_time_t;

static geometry_time_t progress(const watchface_data_t *data)
{
    // 秒不参与位置、面积或角度，确保静态一分钟内不变。
    // Seconds do not affect position, area or angle, keeping each minute static.
    float minute=data->time.tm_min/60.0f;
    return (geometry_time_t){(data->time.tm_hour+minute)/24.0f,
                             ((data->time.tm_hour%12)+minute)/12.0f,minute};
}

static void corners(const wfs_draw_t *d,float x,float y,float side)
{
    float half=side*.5f;
    for(int corner=0;corner<4;corner++){
        float dx=corner==0||corner==3?1:-1,dy=corner<2?1:-1;
        float px=x-dx*half,py=y-dy*half;
        wfs_bar(d,px,py,px+dx*11,py,1.5f,DIM);
        wfs_bar(d,px,py,px,py+dy*11,1.5f,DIM);
    }
}

static void cross(const wfs_draw_t *d,geometry_time_t time)
{
    float x=104+time.day*258,y=364-time.minute*258;
    corners(d,233,235,258);
    wfs_bar(d,x,106,x,364,13,PAPER);
    wfs_bar(d,104,y,362,y,3,MINT);
    wfs_disc(d,x,y,11,0);wfs_disc(d,x,y,7,MINT);
}

static wfs_point_t balance_at(float sine,float cosine,float distance)
{
    return (wfs_point_t){233+sine*distance,224-cosine*distance};
}

static void balance(const wfs_draw_t *d,geometry_time_t time)
{
    float angle=time.hour12*GEOMETRY_TAU,sine=sinf(angle),cosine=cosf(angle);
    wfs_point_t start=balance_at(sine,cosine,-90),end=balance_at(sine,cosine,145);
    wfs_point_t cursor=balance_at(sine,cosine,-90+235*time.minute);
    wfs_line(d,start.x,start.y,end.x,end.y,13,PAPER);
    wfs_disc(d,233,224,18,DIM);wfs_disc(d,233,224,16,0);
    wfs_disc(d,end.x,end.y,4,PAPER);
    wfs_disc(d,cursor.x,cursor.y,15,0);wfs_disc(d,cursor.x,cursor.y,11,GOLD);
}

static void triangle_outline(const wfs_draw_t *d,float base,float tip)
{
    wfs_line(d,112,base,354,base,1.5f,0x3b4140);
    wfs_line(d,354,base,233,tip,1.5f,0x3b4140);
    wfs_line(d,233,tip,112,base,1.5f,0x3b4140);
}

static void triangle_fill(const wfs_draw_t *d,float fraction,bool top)
{
    if(fraction<=0)return;
    // 反解三角形面积：填充深度132*(1-sqrt(1-p))，不能用线性高度替代。
    // Invert triangular area with depth 132*(1-sqrt(1-p)); linear height changes the time encoding.
    float depth=132*(1-sqrtf(1-fraction)),inset=121*depth/132;
    float base=top?94:372,next=base+(top?depth:-depth);
    const wfs_point_t points[]={{112,base},{354,base},{354-inset,next},{112+inset,next}};
    wfs_poly(d,points,4,top?PAPER:MINT);
}

static void hourglass(const wfs_draw_t *d,geometry_time_t time)
{
    triangle_outline(d,94,226);triangle_outline(d,372,240);
    triangle_fill(d,time.hour12,true);triangle_fill(d,time.minute,false);
}

static void square(const wfs_draw_t *d,float x,float y,float maximum,float fraction,uint32_t color)
{
    if(fraction<=0)return;
    // 边长用平方根保持面积线性；浮点多边形保留233px预览的亚像素边界。
    // Square-root side lengths preserve linear area; float polygons retain subpixel edges in 233px previews.
    float half=maximum*sqrtf(fraction)*.5f;
    const wfs_point_t points[]={{x-half,y-half},{x+half,y-half},{x+half,y+half},{x-half,y+half}};
    wfs_poly(d,points,4,color);
}

static void quanta(const wfs_draw_t *d,geometry_time_t time)
{
    corners(d,161,177,164);corners(d,312,310,128);
    square(d,161,177,164,time.day,PAPER);square(d,312,310,128,time.minute,MINT);
}

static bool interval_ge(float a,float b,float threshold,float *left,float *right)
{
    if(fabsf(a)<.000001f)return b>=threshold;
    float crossing=(threshold-b)/a;
    if(a>0)*left=fmaxf(*left,crossing);else *right=fminf(*right,crossing);
    return *left<*right;
}

static void semicircle_body(const wfs_draw_t *d,float radius,float turn,float inset,float tail,uint32_t color)
{
    if(d->size<1||d->size>466)return;
    float scale=d->size/466.0f,cx=233*scale,cy=224*scale,r=radius*scale;
    float angle=turn*GEOMETRY_TAU,nx=sinf(angle),ny=-cosf(angle),vx=-ny,vy=nx;
    float threshold=inset*scale,extension=tail*scale,bound=r+extension;
    int left=LV_MAX(LV_MAX(0,d->layer->_clip_area.x1-d->x),(int)floorf(cx-bound)-1);
    int right=LV_MIN(LV_MIN(d->size-1,d->layer->_clip_area.x2-d->x),(int)ceilf(cx+bound)+1);
    if(left>right)return;
    int top=LV_MAX(LV_MAX(0,d->layer->_clip_area.y1-d->y),(int)floorf(cy-bound));
    int bottom=LV_MIN(LV_MIN(d->size-1,d->layer->_clip_area.y2-d->y),(int)ceilf(cy+bound));
    // 直接求旋转半圆与扫描线交集，共享行缓冲；不建弧点数组，也不分段近似曲线。
    // Intersect the rotated semicircle analytically using shared row scratch, without arc arrays or segmented curves.
    wfs_spans_begin();
    lv_draw_rect_dsc_t rect;lv_draw_rect_dsc_init(&rect);rect.bg_color=lv_color_hex(color);
    for(int row=top;row<=bottom;row++){
        uint16_t *coverage=g_wfs_draw_scratch.coverage;
        memset(coverage+left,0,(size_t)(right-left+1)*sizeof *coverage);
        for(int sample=0;sample<4;sample++){
            float y=row+.125f+sample*.25f-cy;
            float first_left=0,first_right=0,second_left=0,second_right=0;
            bool first=false,second=false;
            if(fabsf(y)<r){
                float reach=sqrtf(r*r-y*y);first_left=-reach;first_right=reach;
                first=interval_ge(nx,ny*y,threshold,&first_left,&first_right);
            }
            if(extension>0){
                // SVG闭合直径的7px黑描边在两端使用默认斜接；尾部矩形保留该外轮廓。
                // The SVG's 7px black stroke uses default miter joins at the diameter; the tail preserves that outline.
                second_left=-bound;second_right=bound;
                second=interval_ge(nx,ny*y,-extension,&second_left,&second_right)
                    &&interval_ge(-nx,-ny*y,0,&second_left,&second_right)
                    &&interval_ge(vx,vy*y,-r,&second_left,&second_right)
                    &&interval_ge(-vx,-vy*y,-r,&second_left,&second_right);
            }
            if(first&&second&&second_left<first_left){
                float temporary=first_left;first_left=second_left;second_left=temporary;
                temporary=first_right;first_right=second_right;second_right=temporary;
            }
            if(first&&second&&first_right>=second_left){first_right=fmaxf(first_right,second_right);second=false;}
            if(first)wfs_cov_span(coverage,left,right,cx+first_left,cx+first_right);
            if(second)wfs_cov_span(coverage,left,right,cx+second_left,cx+second_right);
        }
        for(int x=left;x<=right;){
            unsigned opacity=(coverage[x]+2u)/4u;if(!opacity){x++;continue;}
            int end=x;while(end<right&&(coverage[end+1]+2u)/4u==opacity)end++;
            wfs_span(d,&rect,x,end,row,(lv_opa_t)LV_MIN(255u,opacity));x=end+1;
        }
    }
    wfs_spans_end(d,&rect);
}

static void semicircle(const wfs_draw_t *d,float radius,float turn,uint32_t color)
{
    semicircle_body(d,radius+3.5f,turn,0,3.5f,0);
    semicircle_body(d,radius-3.5f,turn,3.5f,0,color);
}

static void phase(const wfs_draw_t *d,geometry_time_t time)
{
    wfs_disc(d,233,51,2,0x626d66);
    semicircle(d,143,time.hour12,PAPER);semicircle(d,96,time.minute,MINT);
}

static void slab(const wfs_draw_t *d,float left,float right,float sine,float cosine,uint32_t color)
{
    wfs_point_t points[]={{left,116},{right,116},{right,332},{left,332}};
    for(int i=0;i<4;i++){
        float x=points[i].x-233,y=points[i].y-224;
        points[i]=(wfs_point_t){233+cosine*x-sine*y,224+sine*x+cosine*y};
    }
    wfs_poly(d,points,4,color);
}

static void aperture(const wfs_draw_t *d,geometry_time_t time)
{
    float half=(6+102*time.day)*.5f,angle=time.minute*GEOMETRY_TAU;
    float sine=sinf(angle),cosine=cosf(angle);
    // 异色左右片保留完整60分钟方向，缝宽继续编码24小时。
    // Differently colored slabs preserve the full sixty-minute direction; gap width still encodes twenty-four hours.
    slab(d,125,233-half,sine,cosine,PAPER);slab(d,233+half,341,sine,cosine,0x7f998d);
}

void watchface_geometry_render(lv_layer_t *layer,const lv_area_t *area,
                               const watchface_data_t *data,int kind,bool preview)
{
    static const char *const names[]={"CROSS","BALANCE","HOURGLASS","QUANTA","PHASE","APERTURE"};
    if(!data||!area||kind<0||kind>=6)return;
    wfs_draw_t d=wfs_make(layer,area,preview);
    if(data->aod){wfs_aod(&d,data,names[kind]);return;}
    geometry_time_t time=progress(data);
    switch(kind){
    case 0:cross(&d,time);break;
    case 1:balance(&d,time);break;
    case 2:hourglass(&d,time);break;
    case 3:quanta(&d,time);break;
    case 4:phase(&d,time);break;
    case 5:aperture(&d,time);break;
    }
}
