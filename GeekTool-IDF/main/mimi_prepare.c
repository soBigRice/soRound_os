#include "mimi_prepare.h"
#include "mimi_motion.h"
#include <math.h>
#include <string.h>
#define SCREEN MIMI_PREPARE_SIDE
#define EYE_SIZE MIMI_PREPARE_EYE_SIZE
static float clamp(float value, float low, float high) { return fminf(high,fmaxf(low,value)); }
static float mix(float a,float b,float t) { return a+(b-a)*t; }

// 固定上限的扫描线填充支持凹轮廓；四次垂直采样保留亚像素边缘。
// Bounded scanline filling supports concave contours with four vertical AA samples.
static void raster_row(uint8_t *pixels, int row, const mimi_point_t *ring, unsigned count,
                       int first,int last,int16_t coverage_delta[467]) {
    if(row<first||row>=last){memset(pixels,0,EYE_SIZE);return;}
    memset(coverage_delta,0,(size_t)(EYE_SIZE+1)*sizeof coverage_delta[0]);
    for(unsigned sample=0;sample<4;++sample) {
        float y=row+(sample+.5f)*.25f,hits[64];unsigned used=0;
        for(unsigned i=0,j=count-1;i<count;j=i++) {
            mimi_point_t a=ring[j],b=ring[i];
            if((a.y>y)==(b.y>y))continue;
            float x=a.x+(y-a.y)*(b.x-a.x)/(b.y-a.y);unsigned at=used++;
            while(at&&hits[at-1]>x) { hits[at]=hits[at-1];--at; } hits[at]=x;
        }
        for(unsigned i=0;i+1<used;i+=2) {
            float left=fmaxf(0,hits[i]),right=fminf((float)EYE_SIZE,hits[i+1]);
            if(right<=left)continue;
            int start=(int)floorf(left),end=(int)ceilf(right)-1;
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
    for(int x=0;x<EYE_SIZE;++x) {alpha+=coverage_delta[x];pixels[x]=(uint8_t)(alpha>255?255:alpha);}
}

bool mimi_prepare_eyes_rows(const mimi_pet_t *pet,uint32_t pose_ms,mimi_prepare_body_row_fn read_body,void *context,
                            int16_t delta[MIMI_PREPARE_SIDE+1],mimi_prepared_eyes_t *out) {
    if(!pet||!read_body||!delta||!out||pet->motion>=MIMI_MOTION_COUNT||
       pose_ms>mimi_motion_duration(pet->motion)+450u||pose_ms%50||
       !isfinite(pet->gaze_x)||!isfinite(pet->gaze_y)||fabsf(pet->gaze_x)>1||fabsf(pet->gaze_y)>1||
       !isfinite(pet->lid)||pet->lid<0||pet->lid>1)return false;
    for(unsigned eye=0;eye<2;++eye)for(unsigned i=0;i<MIMI_EYE_POINTS;++i)
        if(!isfinite(pet->eyes[eye][i].x)||!isfinite(pet->eyes[eye][i].y)||
           fabsf(pet->eyes[eye][i].x)>10000||fabsf(pet->eyes[eye][i].y)>10000)return false;
    // 身体素材与眼睛必须使用同一量化姿态；普通眼睛/注视/眼睑仍来自这次实时快照。
    // Body assets and eyes share one quantized pose; ordinary eyes, gaze and lids retain this live snapshot.
    mimi_pet_t pose=*pet;pose.motion_ms=pose_ms;
    float motion_weight=mimi_pet_motion_mix(&pose);
    mimi_motion_pose_t scene;mimi_motion_sample(pet->motion,pose_ms,&scene);
    float motion_radius=pet->motion==MIMI_MOTION_ORBIT||pet->motion==MIMI_MOTION_PLAY?164:
                        pet->motion==MIMI_MOTION_NOTIFY?195:220;
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
        float min_y=(float)EYE_SIZE,max_y=0;
        for(unsigned i=0;i<48;++i){min_y=fminf(min_y,ring[i].y);max_y=fmaxf(max_y,ring[i].y);}
        int first=(int)clamp(floorf(min_y),0,(float)EYE_SIZE),last=(int)clamp(ceilf(max_y),0,(float)EYE_SIZE);
        float alpha=mix(1,scene.eye_opa[eye],motion_weight);
        int left=EYE_SIZE,top=EYE_SIZE,right=-1,bottom=-1;
        uint8_t alpha_row[EYE_SIZE],body_row[SCREEN];
        for(int yy=0;yy<EYE_SIZE;++yy){
            raster_row(alpha_row,yy,ring,48,first,last,delta);
            bool body_loaded=false;
            // 全透明输出不需要身体行；仍保留相同的轮廓与边界元数据计算。
            // Fully transparent output needs no body row while preserving outline and bounds evaluation.
            if(alpha==0)memset(alpha_row,0,EYE_SIZE);
            else for(int xx=0;xx<EYE_SIZE;++xx){
                if(!alpha_row[xx])continue;
                if(x+xx<0||x+xx>=SCREEN||y+yy<0||y+yy>=SCREEN)alpha_row[xx]=0;
                else {
                    if(!body_loaded){
                        if(!read_body(context,(unsigned)(y+yy),body_row))return false;
                        body_loaded=true;
                    }
                    uint8_t body_alpha=body_row[x+xx];
                    if(!body_alpha)alpha_row[xx]=0;
                    else if(body_alpha!=255||alpha!=1)alpha_row[xx]=(uint8_t)lroundf(alpha_row[xx]*(body_alpha/255.f)*alpha);
                }
                if(alpha_row[xx]){left=left<xx?left:xx;right=right>xx?right:xx;top=top<yy?top:yy;bottom=bottom>yy?bottom:yy;}
            }
            memcpy(out->alpha[eye]+yy*EYE_SIZE,alpha_row,EYE_SIZE);
        }
        if(right<left)left=top=right=bottom=0;
        out->eye[eye]=(mimi_prepared_eye_bounds_t){(int16_t)(x+left),(int16_t)(y+top),
            (uint16_t)left,(uint16_t)top,(uint16_t)(right-left+1),(uint16_t)(bottom-top+1)};
    }
    return true;
}

typedef struct {const uint8_t *pixels;} full_body_t;
static bool full_body_row(void *context,unsigned y,uint8_t row[SCREEN]){
    const full_body_t *body=context;
    if(y>=SCREEN)return false;
    memcpy(row,body->pixels+y*SCREEN,SCREEN);return true;
}
bool mimi_prepare_eyes(const mimi_pet_t *pet,uint32_t pose_ms,const uint8_t *body,
                       int16_t delta[MIMI_PREPARE_SIDE+1],mimi_prepared_eyes_t *out){
    if(!body)return false;
    full_body_t context={body};return mimi_prepare_eyes_rows(pet,pose_ms,full_body_row,&context,delta,out);
}
