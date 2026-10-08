// CPU design solver: semi-Lagrangian advection + pressure projection (Stam, Stable Fluids).
// This is a visual dye model, not a quantitative hydrodynamics solver. Grid resolution is
// independent of the native 466px output; PSRAM allocation/lifecycle belongs to app_fluid.
#include "fluid_ink.h"
#include <math.h>
#include <string.h>

#define FN FLUID_INK_GRID
#define FC (FN*FN)
#define FW FLUID_INK_WIDTH
#define PI 3.14159265358979323846f
struct fluid_ink {
    float u[FC],v[FC],su[FC],sv[FC],pressure[FC],divergence[FC],curl[FC];
    float dye[3][FC],scratch[3][FC];
    uint8_t mask[FC],rgb[FC][3],gamma[256];
    float gaussian[256];
    uint16_t row_a[FW][3],row_b[FW][3],map_cell[FW],map_frac[FW];
    int16_t spans[FW];
    unsigned palette;
};
static const float colors[3][3][3]={
    {{.20f,.46f,.70f},{.30f,.70f,.68f},{.85f,.43f,.23f}},
    {{.22f,.53f,.43f},{.56f,.70f,.47f},{.78f,.59f,.32f}},
    {{.40f,.39f,.65f},{.70f,.45f,.61f},{.86f,.48f,.34f}}
};
static float bounded(float v,float lo,float hi){return v<lo?lo:v>hi?hi:v;}
size_t fluid_ink_bytes(void){return sizeof(fluid_ink_t);}
static float sample(const float *field,float x,float y){
    x=bounded(x,0,FN-1.001f);y=bounded(y,0,FN-1.001f);
    int ix=(int)x,iy=(int)y,k=iy*FN+ix;float a=x-ix,b=y-iy;
    return (field[k]*(1-a)+field[k+1]*a)*(1-b)+(field[k+FN]*(1-a)+field[k+FN+1]*a)*b;
}
void fluid_ink_inject(fluid_ink_t *s,float x,float y,float dx,float dy,unsigned color,float dose){
    if(!s||!isfinite(x)||!isfinite(y)||!isfinite(dx)||!isfinite(dy)||!isfinite(dose)||hypotf(x-.5f,y-.5f)>.44f)return;
    dx=bounded(dx,-180,180);dy=bounded(dy,-180,180);dose=bounded(dose,0,1);
    float px=x*(FN-1),py=y*(FN-1);
    int x1=(int)bounded(px-9,1,FN-2),x2=(int)bounded(px+9,1,FN-2);
    int y1=(int)bounded(py-9,1,FN-2),y2=(int)bounded(py+9,1,FN-2);
    const float *color_value=colors[s->palette][color%3];
    for(int y0=y1;y0<=y2;++y0)for(int x0=x1;x0<=x2;++x0){
        int k=y0*FN+x0;if(!s->mask[k])continue;
        float ex=(x0-px)/(FN-1),ey=(y0-py)/(FN-1),q=(ex*ex+ey*ey)/.0023f;
        int lookup=(int)(q*25.5f);if(lookup>=256)continue;float amount=s->gaussian[lookup];
        s->u[k]=bounded(s->u[k]+dx*amount,-180,180);s->v[k]=bounded(s->v[k]+dy*amount,-180,180);
        for(int c=0;c<3;++c)s->dye[c][k]+=color_value[c]*amount*dose;
    }
}
void fluid_ink_reset(fluid_ink_t *s,unsigned palette){
    if(!s)return;
    memset(s,0,sizeof(*s));s->palette=palette%FLUID_INK_PALETTES;
    for(int y=0;y<FN;++y)for(int x=0;x<FN;++x){float dx=x/(float)(FN-1)-.5f,dy=y/(float)(FN-1)-.5f;s->mask[y*FN+x]=dx*dx+dy*dy<.46f*.46f;}
    for(int i=0;i<256;++i){s->gamma[i]=(uint8_t)lroundf(powf(i/255.0f,.75f)*255);s->gaussian[i]=expf(-i/25.5f);}
    for(int i=0;i<FW;++i){float p=i*(FN-1.001f)/(FW-1);s->map_cell[i]=(uint16_t)p;s->map_frac[i]=(uint16_t)((p-s->map_cell[i])*256);
        float y=i-232.5f;s->spans[i]=fabsf(y)>217?-1:(int16_t)sqrtf(217*217-y*y);}
    for(unsigned band=0;band<3;++band)for(int i=0;i<18;++i){
        float t=i/17.0f,a=t*PI*1.65f+band*PI*.66f,r=.08f+t*.22f;
        fluid_ink_inject(s,.5f+cosf(a)*r,.53f-sinf(a)*r,-sinf(a)*26,-cosf(a)*26,band,.75f);
    }
}
float fluid_ink_step(fluid_ink_t *s,float seconds){
    if(!s||!isfinite(seconds)||seconds<=0)return 0;
    float dt=fminf(seconds,.025f),drag=1/(1+dt*.65f);
    for(int y=1;y<FN-1;++y)for(int x=1;x<FN-1;++x){int k=y*FN+x;if(!s->mask[k])continue;
        float bx=x-dt*s->u[k],by=y-dt*s->v[k];s->su[k]=sample(s->u,bx,by)*drag;s->sv[k]=sample(s->v,bx,by)*drag;}
    memcpy(s->u,s->su,sizeof(s->u));memcpy(s->v,s->sv,sizeof(s->v));
    for(int k=FN+1;k<FC-FN-1;++k)if(s->mask[k])s->curl[k]=.5f*(s->v[k+1]-s->v[k-1]-s->u[k+FN]+s->u[k-FN]);
    for(int k=FN+1;k<FC-FN-1;++k)if(s->mask[k]){
        float nx=.5f*(fabsf(s->curl[k+FN])-fabsf(s->curl[k-FN]));
        float ny=.5f*(fabsf(s->curl[k-1])-fabsf(s->curl[k+1]));
        float scale=s->curl[k]*dt*2/(sqrtf(nx*nx+ny*ny)+.0001f);
        s->u[k]=bounded(s->u[k]+nx*scale,-180,180);s->v[k]=bounded(s->v[k]+ny*scale,-180,180);
    }
    memset(s->pressure,0,sizeof(s->pressure));
    for(int k=FN+1;k<FC-FN-1;++k)if(s->mask[k])s->divergence[k]=.5f*(s->u[k+1]-s->u[k-1]+s->v[k+FN]-s->v[k-FN]);
    // No-through-flow circular walls. A fixed iteration budget bounds each UI frame.
    for(int iteration=0;iteration<12;++iteration)for(int k=FN+1;k<FC-FN-1;++k)if(s->mask[k]){
        const int neighbours[]={k-1,k+1,k-FN,k+FN};float sum=0;unsigned count=0;
        for(int n=0;n<4;++n)if(s->mask[neighbours[n]]){sum+=s->pressure[neighbours[n]];++count;}
        s->pressure[k]=(sum-s->divergence[k])/count;
    }
    float maximum=0;
    for(int k=FN+1;k<FC-FN-1;++k)if(s->mask[k]){
        float p=s->pressure[k],l=s->mask[k-1]?s->pressure[k-1]:p,r=s->mask[k+1]?s->pressure[k+1]:p;
        float t=s->mask[k-FN]?s->pressure[k-FN]:p,b=s->mask[k+FN]?s->pressure[k+FN]:p;
        float u=s->u[k]-.5f*(r-l),v=s->v[k]-.5f*(b-t);
        if((!s->mask[k-1]&&u<0)||(!s->mask[k+1]&&u>0))u=0;
        if((!s->mask[k-FN]&&v<0)||(!s->mask[k+FN]&&v>0))v=0;
        s->u[k]=u;s->v[k]=v;maximum=fmaxf(maximum,u*u+v*v);
    }
    for(int y=1;y<FN-1;++y)for(int x=1;x<FN-1;++x){int k=y*FN+x;if(!s->mask[k])continue;
        float bx=x-dt*s->u[k],by=y-dt*s->v[k];for(int c=0;c<3;++c)s->scratch[c][k]=sample(s->dye[c],bx,by);}
    memcpy(s->dye,s->scratch,sizeof(s->dye));return sqrtf(maximum);
}
static void horizontal(fluid_ink_t *s,int row,uint16_t output[FW][3]){
    for(int x=0;x<FW;++x){int k=row*FN+s->map_cell[x],f=s->map_frac[x];
        for(int c=0;c<3;++c)output[x][c]=s->rgb[k][c]*(256-f)+s->rgb[k+1][c]*f;}
}
bool fluid_ink_render(fluid_ink_t *s,uint16_t *buffer,fluid_ink_dirty_t dirty[FLUID_INK_BANDS]){
    if(!s||!buffer||!dirty)return false;
    for(int i=0;i<FLUID_INK_BANDS;++i)dirty[i]=(fluid_ink_dirty_t){FW,FW,-1,-1};
    for(int k=0;k<FC;++k){float density=fmaxf(s->dye[0][k],fmaxf(s->dye[1][k],s->dye[2][k]));
        float t=bounded((density-.025f)/.165f,0,1),alpha=t*t*(3-2*t),scale=255/(.35f+density);
        for(int c=0;c<3;++c)s->rgb[k][c]=(uint8_t)(s->gamma[(int)bounded(s->dye[c][k]*scale,0,255)]*alpha);}
    bool changed=false;int previous=-1;
    for(int y=0;y<FW;++y){int span=s->spans[y];if(span<0)continue;int row=s->map_cell[y],f=s->map_frac[y];
        if(row!=previous){horizontal(s,row,s->row_a);horizontal(s,row+1,s->row_b);previous=row;}
        int left=233-span,right=232+span;
        for(int x=left;x<=right;++x){unsigned rgb[3];for(int c=0;c<3;++c)rgb[c]=(s->row_a[x][c]*(256-f)+s->row_b[x][c]*f)>>16;
            uint16_t pixel=((rgb[0]>>3)<<11)|((rgb[1]>>2)<<5)|(rgb[2]>>3);int k=y*FW+x;
            if(buffer[k]==pixel)continue;
            buffer[k]=pixel;changed=true;
            fluid_ink_dirty_t *area=&dirty[y*FLUID_INK_BANDS/FW];
            if(x<area->x1)area->x1=x;
            if(x>area->x2)area->x2=x;
            if(y<area->y1)area->y1=y;
            if(y>area->y2)area->y2=y;
        }
    }
    return changed;
}
