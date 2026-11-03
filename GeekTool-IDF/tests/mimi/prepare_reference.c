// 原完整扫描线参考，冻结自app_mimi_reference.c SHA6ccdb6f4；与逐行优化算法独立。
// Original full-scanline oracle frozen from app_mimi_reference.c SHA6ccdb6f4, independent of row preparation.
#include "mimi_prepare.h"
#include "mimi_motion.h"
#include <math.h>
#include <string.h>
#define SCREEN 466
#define EYE_SIZE 208
#define TAU 6.28318530718f
#define LV_MIN(a,b) ((a)<(b)?(a):(b))
#define LV_MAX(a,b) ((a)>(b)?(a):(b))
static int16_t coverage_delta[467];
static float clamp(float value, float low, float high) { return fminf(high,fmaxf(low,value)); }
static float mix(float a,float b,float t) { return a+(b-a)*t; }

// 固定上限的扫描线填充支持凹轮廓；四次垂直采样保留亚像素边缘。
// Bounded scanline filling supports concave contours with four vertical AA samples.
static void raster(uint8_t *pixels, int width, int height, const mimi_point_t *ring, unsigned count) {
    memset(pixels,0,(size_t)width*height);
    float min_y=(float)height,max_y=0;
    for(unsigned i=0;i<count;++i) { min_y=fminf(min_y,ring[i].y); max_y=fmaxf(max_y,ring[i].y); }
    int first=(int)clamp(floorf(min_y),0,(float)height),last=(int)clamp(ceilf(max_y),0,(float)height);
    for(int row=first;row<last;++row) {
      memset(coverage_delta,0,(size_t)(width+1)*sizeof coverage_delta[0]);
      for(unsigned sample=0;sample<4;++sample) {
        float y=row+(sample+.5f)*.25f,hits[64];unsigned used=0;
        for(unsigned i=0,j=count-1;i<count;j=i++) {
            mimi_point_t a=ring[j],b=ring[i];
            if((a.y>y)==(b.y>y))continue;
            float x=a.x+(y-a.y)*(b.x-a.x)/(b.y-a.y);unsigned at=used++;
            while(at&&hits[at-1]>x) { hits[at]=hits[at-1];--at; } hits[at]=x;
        }
        for(unsigned i=0;i+1<used;i+=2) {
            float left=fmaxf(0,hits[i]),right=fminf((float)width,hits[i+1]);
            if(right<=left)continue;
            int start=(int)floorf(left),end=(int)ceilf(right)-1;
            // 仅两端需要浮点覆盖率；内部每采样贡献 64，与原 lroundf(63.75) 一致。
            // Only endpoints need fractional coverage; interior samples add 64, exactly as lroundf(63.75).
            for(unsigned edge=0;edge<(start==end?1u:2u);++edge) {
                int x=edge?end:start;
                float area=fmaxf(0,fminf(x+1.f,right)-fmaxf((float)x,left));
                int16_t alpha=(int16_t)lroundf(area*63.75f);
                coverage_delta[x]+=alpha;coverage_delta[x+1]-=alpha;
            }
            if(end>start+1) { coverage_delta[start+1]+=64;coverage_delta[end]-=64; }
        }
      }
      int alpha=0;
      for(int x=0;x<width;++x) { alpha+=coverage_delta[x];pixels[row*width+x]=(uint8_t)(alpha>255?255:alpha); }
    }
}
void prepare_reference_body(const mimi_pet_t *source,uint32_t pose_ms,uint8_t *out){
 mimi_pet_t quantized=*source;quantized.motion_ms=pose_ms;const mimi_pet_t *pet=&quantized;
 float motion_weight=mimi_pet_motion_mix(pet);
 float motion_radius=pet->motion==MIMI_MOTION_ORBIT||pet->motion==MIMI_MOTION_PLAY?164:
                     pet->motion==MIMI_MOTION_NOTIFY?195:220;
 mimi_motion_pose_t scene;mimi_motion_sample(pet->motion,pose_ms,&scene);

 mimi_point_t ring[64];
 for(unsigned i=0;i<64;++i){
  float angle=i*TAU/64;
  ring[i]=(mimi_point_t){232.5f+mix(cosf(angle)*233,scene.body[i].x*motion_radius,motion_weight),
                        232.5f+mix(sinf(angle)*233,scene.body[i].y*motion_radius,motion_weight)};
 }
 raster(out,SCREEN,SCREEN,ring,64);
}
void prepare_reference_eyes(const mimi_pet_t *source,uint32_t pose_ms,const uint8_t *body,mimi_prepared_eyes_t *out){
 mimi_pet_t quantized=*source;quantized.motion_ms=pose_ms;const mimi_pet_t *pet=&quantized;
 float motion_weight=mimi_pet_motion_mix(pet);
 float motion_radius=pet->motion==MIMI_MOTION_ORBIT||pet->motion==MIMI_MOTION_PLAY?164:
                     pet->motion==MIMI_MOTION_NOTIFY?195:220;
 mimi_motion_pose_t scene;mimi_motion_sample(pet->motion,pose_ms,&scene);
    for(unsigned eye=0;eye<2;++eye) {
        mimi_point_t center={0},ring[48];
        for(unsigned i=0;i<48;++i) {
            float x=(pet->eyes[eye][i].x-114.2705f)*(466/228.541f)+pet->gaze_x*20;
            float y=(pet->eyes[eye][i].y-114.2705f)*(466/228.541f)+pet->gaze_y*14;
            ring[i]=(mimi_point_t){232.5f+mix(x,scene.eyes[eye][i].x*motion_radius,motion_weight),
                232.5f+mix(y,scene.eyes[eye][i].y*motion_radius,motion_weight)};
            center.x+=ring[i].x/48;center.y+=ring[i].y/48;
        }
        int x=(int)lroundf(center.x)-EYE_SIZE/2,y=(int)lroundf(center.y)-EYE_SIZE/2;
        for(unsigned i=0;i<48;++i) {
            ring[i].x-=x;
            ring[i].y=center.y+(ring[i].y-center.y)*mix(fmaxf(.06f,pet->lid),1,motion_weight)-y;
        }
        uint8_t *bitmap=out->alpha[eye];
        raster(bitmap,EYE_SIZE,EYE_SIZE,ring,48);
        float alpha=mix(1,scene.eye_opa[eye],motion_weight);
        int left=EYE_SIZE,top=EYE_SIZE,right=-1,bottom=-1;
        // 使用当前身体遮罩裁切，变形过程中眼睛也不能逸出轮廓。
        // Clip eyes against the current body mask throughout silhouette morphs.
        for(int yy=0;yy<EYE_SIZE;++yy) for(int xx=0;xx<EYE_SIZE;++xx) {
            unsigned at=(unsigned)(yy*EYE_SIZE+xx);
            if(!bitmap[at])continue;
            if(x+xx<0||x+xx>=SCREEN||y+yy<0||y+yy>=SCREEN)bitmap[at]=0;
            else {
                uint8_t body_alpha=body[(y+yy)*SCREEN+x+xx];
                if(!body_alpha)bitmap[at]=0;
                else if(body_alpha!=255||alpha!=1)bitmap[at]=(uint8_t)lroundf(bitmap[at]*(body_alpha/255.f)*alpha);
            }
            if(bitmap[at]) { left=LV_MIN(left,xx);right=LV_MAX(right,xx);top=LV_MIN(top,yy);bottom=LV_MAX(bottom,yy); }
        }
        if(right<left)left=top=right=bottom=0;
        // 图像只引用有效眼睛边界，旧位置先失效；保留原208字节行跨度与所有非零AA像素。
        // Reference only the eye's ink bounds after invalidating its old area; retain the 208-byte stride and every AA pixel.
        out->eye[eye]=(mimi_prepared_eye_bounds_t){(int16_t)(x+left),(int16_t)(y+top),
            (uint16_t)left,(uint16_t)top,(uint16_t)(right-left+1),(uint16_t)(bottom-top+1)};
    }
}
