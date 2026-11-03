#include "app.h"
#include "font_mimi.h"
#include "mimi_motion.h"
#include "settings.h"
#include "tools_ui.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "esp_heap_caps.h"
#include <math.h>
#include <string.h>

// 原生素材生成沿用本文件的矢量路径；设备和完整主机回归另行启用后台加速。
// Asset generation reuses this file's vector path; devices and full integration tests enable background acceleration separately.
#if !defined(MIMI_REFERENCE_ONLY) && (defined(ESP_PLATFORM) || defined(MIMI_FULL_HOST))
#define MIMI_ACCELERATED 1
#include "mimi_pipeline.h"
#include "mimi_render.h"
#include "resource_store.h"
#ifdef ESP_PLATFORM
#include "esp_log.h"
#else
#include <stdio.h>
#endif
#else
#define MIMI_ACCELERATED 0
#endif

#define SCREEN 466
#define EYE_SIZE 208
#define BODY_BYTES (SCREEN * SCREEN)
#define PIXEL_BYTES (BODY_BYTES + 2 * EYE_SIZE * EYE_SIZE)
#define TAU 6.28318530718f

typedef struct {
    lv_point_precise_t points[6][65];
    uint32_t colors[6][65];
    bool behind[6][65];
    uint32_t covered[6][2];
    int32_t width[6];
    uint8_t opa[6];
    unsigned count;
} mimi_decor_cache_t;
#define MASK_BYTES (PIXEL_BYTES + sizeof(mimi_decor_cache_t))

static mimi_pet_t pet;
static mimi_motion_pose_t scene;
static mimi_point_t body_ring[64], previous_body[64];
static lv_obj_t *root, *body, *eyes[2], *decor[2], *return_button, *error_label;
static lv_image_dsc_t images[3];
static uint8_t *masks;
static mimi_decor_cache_t *decor_cache;
// LVGL 串行调用；行差分留在内部 RAM，避免栈增长和逐内部像素浮点运算。
// LVGL serializes calls; internal-RAM row differences avoid stack growth and per-interior-pixel floats.
static int16_t coverage_delta[SCREEN+1];
static uint32_t last_tick, pressed_at, return_at;
static bool visible, pressed, consumed, body_ready, decor_was_active;
static lv_point_t previous;
static float stroke_distance, motion_weight, motion_radius;

#if MIMI_ACCELERATED
static mimi_pipeline_t *accel_pipeline;
static mimi_render_t *accel_renderer;
static lv_obj_t *accel_image;
static lv_image_dsc_t accel_descriptor;
static const mimi_pipeline_frame_t *accel_published;
static mimi_pipeline_stats_t accel_exit_stats;
static bool accel_show,accel_requesting,accel_disabled;
static unsigned accel_clip;

static void accel_warning(const char *reason) {
#ifdef ESP_PLATFORM
    ESP_LOGW("mimi","%s; preserving the complete vector renderer",reason);
#else
    fprintf(stderr,"mimi: %s; preserving the complete vector renderer\n",reason);
#endif
}
static void accel_hide(void) {
    if(accel_renderer&&accel_show)mimi_render_attach(accel_renderer,NULL);
    if(accel_image)ui_obj_set_hidden(accel_image,true);
    accel_show=false;accel_published=NULL;body_ready=false;
}
static bool accel_render(void) {
    bool eligible=pet.motion_active&&(pet.motion==MIMI_MOTION_PLAY||pet.motion==MIMI_MOTION_ORBIT||pet.motion==MIMI_MOTION_COMET);
    if(!accel_pipeline||accel_disabled||!eligible){
        if(accel_requesting&&accel_pipeline)mimi_pipeline_invalidate(accel_pipeline);
        accel_requesting=false;
        if(accel_show)accel_hide();
        return false;
    }
    if(!accel_requesting){mimi_pipeline_invalidate(accel_pipeline);accel_requesting=true;}
    if(accel_clip!=pet.motion){accel_hide();accel_clip=pet.motion;}
    bool fresh=false;
    const mimi_pipeline_frame_t *frame=mimi_pipeline_update(accel_pipeline,&pet,&fresh);
    mimi_pipeline_stats_t stats;mimi_pipeline_stats(accel_pipeline,&stats);
    if(stats.errors){accel_disabled=true;accel_hide();accel_warning("asset preparation failed");return false;}
    // 首帧尚在准备时仍可使用完整矢量路径；已显示帧保持到下一帧完整就绪。
    // Keep the complete vector first frame while warming up, then retain each published frame until its successor is ready.
    if(!frame)return accel_show;
    if(fresh||frame!=accel_published||!accel_show){
        if(!mimi_render_select(accel_renderer,&frame->asset.frame,&frame->eyes)||
           (!accel_show&&!mimi_render_attach(accel_renderer,&accel_descriptor))){
            accel_disabled=true;accel_hide();accel_warning("renderer activation failed");return false;
        }
        ui_obj_set_hidden(body,true);
        for(unsigned e=0;e<2;e++){ui_obj_set_hidden(eyes[e],true);ui_obj_set_hidden(decor[e],true);}
        ui_obj_set_hidden(accel_image,false);lv_obj_invalidate(accel_image);
        accel_published=frame;accel_show=true;body_ready=false;
    }
    return true;
}
#endif

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
static void dot(lv_layer_t *layer,float x,float y,float radius,uint32_t color,float alpha) {
    if(radius<.5f||alpha<=0)return;
    int size=(int)lroundf(radius*2);
    lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);
    d.radius=LV_RADIUS_CIRCLE;d.bg_color=lv_color_hex(color);d.bg_opa=(lv_opa_t)lroundf(clamp(alpha,0,1)*255);
    lv_area_t a={(int)lroundf(x)-size/2,(int)lroundf(y)-size/2,0,0};
    a.x2=a.x1+size-1;a.y2=a.y1+size-1;lv_draw_rect(layer,&d,&a);
}
static uint32_t wheel(float hue) {
    float h=fmodf(hue,360);if(h<0)h+=360;
    float c=(1-fabsf(2*.62f-1))*.55f,x=c*(1-fabsf(fmodf(h/60,2)-1)),m=.62f-c/2;
    float r=0,g=0,b=0;
    if(h<60){r=c;g=x;}else if(h<120){r=x;g=c;}else if(h<180){g=c;b=x;}
    else if(h<240){g=x;b=c;}else if(h<300){r=x;b=c;}else{r=c;b=x;}
    return ((uint32_t)lroundf((r+m)*255)<<16)|((uint32_t)lroundf((g+m)*255)<<8)|(uint32_t)lroundf((b+m)*255);
}
static lv_area_t line_bounds(lv_point_precise_t a,lv_point_precise_t b,int32_t width) {
    return (lv_area_t){(int32_t)LV_MIN(a.x,b.x)-width,(int32_t)LV_MIN(a.y,b.y)-width,
        (int32_t)LV_MAX(a.x,b.x)+width,(int32_t)LV_MAX(a.y,b.y)+width};
}
static bool body_covers(lv_area_t area,lv_area_t root_bounds) {
    area.x1-=root_bounds.x1;area.x2-=root_bounds.x1;area.y1-=root_bounds.y1;area.y2-=root_bounds.y1;
    if(area.x1<0||area.y1<0||area.x2>=SCREEN||area.y2>=SCREEN)return false;
    for(int y=area.y1;y<=area.y2;++y)for(int x=area.x1;x<=area.x2;++x)
        if(masks[y*SCREEN+x]!=255)return false;
    return true;
}
static void prepare_decor(void) {
    decor_cache->count=0;
    memset(decor_cache->covered,0,sizeof decor_cache->covered);
    if(motion_weight<=0)return;
    lv_area_t bounds;lv_obj_get_coords(root,&bounds);
    float ox=bounds.x1+232.5f,oy=bounds.y1+232.5f;
    float t=pet.motion_ms*.001f;
    const mimi_arc_seed_t *seeds=mimi_motion_arcs(scene.arc_kind,&decor_cache->count);
    for(unsigned arc=0;arc<decor_cache->count;++arc) {
        float alpha=scene.arc_opa[arc]*motion_weight;if(alpha<.01f)continue;
        const mimi_arc_seed_t *seed=&seeds[arc];
        float tilt_cos=cosf(seed->tilt),tilt_sin=sinf(seed->tilt),phase=seed->phase+t*seed->speed*TAU;
        float cs=cosf(phase),sn=sinf(phase),delta=seed->sweep*TAU/64,dc=cosf(delta),ds=sinf(delta);
        float cx=scene.arc_kind==2?.45f-t*.42f:seed->cx;
        decor_cache->width[arc]=(int32_t)fmaxf(1,seed->width*motion_radius);
        decor_cache->opa[arc]=(lv_opa_t)lroundf(alpha*255);
        for(unsigned i=0;i<=64;++i) {
            float x=seed->a*(cs*tilt_cos-sn*tilt_sin*seed->k)+cx;
            float y=seed->a*(cs*tilt_sin+sn*tilt_cos*seed->k)+seed->cy;
            // 保留原坐标运算顺序和 LVGL 坐标转换，避免浮点舍入使边缘跳动一像素。
            // Preserve the original coordinate expression and LVGL conversion to avoid one-pixel rounding drift.
            decor_cache->points[arc][i]=(lv_point_precise_t){ox+x*motion_radius,oy+y*motion_radius};
            decor_cache->behind[arc][i]=sn<0;
            decor_cache->colors[arc][i]=wheel(seed->hue+seed->hue_span*(cs+1)*.5f);
            // 仅跳过包围盒被身体完全不透明覆盖的后景线，凹轮廓逐像素核对。
            // Cull a rear line only when the opaque body covers its entire bounds, including concave contours.
            if(i&&decor_cache->behind[arc][i]&&decor_cache->behind[arc][i-1]&&
               body_covers(line_bounds(decor_cache->points[arc][i-1],decor_cache->points[arc][i],decor_cache->width[arc]),bounds))
                decor_cache->covered[arc][(i-1)/32]|=1u<<((i-1)%32);
            float next=cs*dc-sn*ds;sn=sn*dc+cs*ds;cs=next;
        }
    }
}
static void draw_decor(lv_event_t *event) {
    if(!masks||motion_weight<=0)return;
    bool behind=(uintptr_t)lv_event_get_user_data(event)==0;
    lv_layer_t *layer=lv_event_get_layer(event);lv_area_t bounds;lv_obj_get_coords(root,&bounds);
    float ox=bounds.x1+232.5f,oy=bounds.y1+232.5f;
    // 每帧只准备一次几何；40 行分块只提交真正相交的线段，保留全部 64 段和彩色渐变。
    // Prepare geometry once per frame; 40-row tiles submit intersecting lines with all 64 segments and colors intact.
    for(unsigned arc=0;arc<decor_cache->count;++arc) {
        if(scene.arc_opa[arc]*motion_weight<.01f)continue;
        lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);d.round_start=d.round_end=true;
        d.width=decor_cache->width[arc];d.opa=decor_cache->opa[arc];
        for(unsigned i=1;i<=64;++i) {
            if(decor_cache->behind[arc][i]!=behind||decor_cache->behind[arc][i-1]!=behind)continue;
            if(decor_cache->covered[arc][(i-1)/32]&(1u<<((i-1)%32)))continue;
            d.p1=decor_cache->points[arc][i-1];d.p2=decor_cache->points[arc][i];
            // 与 LVGL 9.5 lv_draw_line 的包围盒相同，不裁掉圆角或抗锯齿边缘。
            // Match LVGL 9.5 lv_draw_line bounds so rounded caps and AA edges are never culled.
            lv_area_t area=line_bounds(d.p1,d.p2,d.width);
            const lv_area_t *clip=&layer->_clip_area;
            if(area.x1>clip->x2||area.x2<clip->x1||area.y1>clip->y2||area.y2<clip->y1)continue;
            d.color=lv_color_hex(decor_cache->colors[arc][i]);lv_draw_line(layer,&d);
        }
    }
    if(behind==scene.dots_behind) for(unsigned i=0;i<5;++i)
        dot(layer,ox+scene.dots[i][0]*motion_radius,oy+scene.dots[i][1]*motion_radius,
            scene.dots[i][2]*motion_radius,TOOLS_WHITE,scene.dot_opa[i]*motion_weight);
    if(!behind&&scene.notch[2]>0) {
        float x=ox+scene.notch[0]*motion_radius,y=oy+scene.notch[1]*motion_radius;
        dot(layer,x,y,scene.notch[3]*motion_radius,COL_BG,motion_weight);
        dot(layer,x,y,scene.notch[2]*motion_radius,0x2496e8,motion_weight);
    }
}
static void render_pet(void) {
    if(!masks)return;
#if MIMI_ACCELERATED
    if(accel_render())return;
    ui_obj_set_hidden(body,false);
    for(unsigned e=0;e<2;e++){ui_obj_set_hidden(eyes[e],false);ui_obj_set_hidden(decor[e],false);}
#endif
    motion_weight=mimi_pet_motion_mix(&pet);
    mimi_motion_sample(pet.motion,pet.motion_ms,&scene);
    motion_radius=pet.motion==MIMI_MOTION_ORBIT||pet.motion==MIMI_MOTION_PLAY?164:
                  pet.motion==MIMI_MOTION_NOTIFY?195:220;
    for(unsigned i=0;i<64;++i) {
        float angle=i*TAU/64;
        body_ring[i]=(mimi_point_t){232.5f+mix(cosf(angle)*233,scene.body[i].x*motion_radius,motion_weight),
            232.5f+mix(sinf(angle)*233,scene.body[i].y*motion_radius,motion_weight)};
    }
    // 静态满屏身体只栅格化一次；动作中仅重建真正变化的轮廓。
    // Rasterize the static full-screen body once; rebuild only changed motion contours.
    if(!body_ready||memcmp(previous_body,body_ring,sizeof body_ring)) {
        lv_image_cache_drop(&images[0]);raster(masks,SCREEN,SCREEN,body_ring,64);
        memcpy(previous_body,body_ring,sizeof body_ring);body_ready=true;lv_obj_invalidate(body);
    }
    for(unsigned eye=0;eye<2;++eye) {
        mimi_point_t center={0},ring[48];
        for(unsigned i=0;i<48;++i) {
            float x=(pet.eyes[eye][i].x-114.2705f)*(466/228.541f)+pet.gaze_x*20;
            float y=(pet.eyes[eye][i].y-114.2705f)*(466/228.541f)+pet.gaze_y*14;
            ring[i]=(mimi_point_t){232.5f+mix(x,scene.eyes[eye][i].x*motion_radius,motion_weight),
                232.5f+mix(y,scene.eyes[eye][i].y*motion_radius,motion_weight)};
            center.x+=ring[i].x/48;center.y+=ring[i].y/48;
        }
        int x=(int)lroundf(center.x)-EYE_SIZE/2,y=(int)lroundf(center.y)-EYE_SIZE/2;
        for(unsigned i=0;i<48;++i) {
            ring[i].x-=x;
            ring[i].y=center.y+(ring[i].y-center.y)*mix(fmaxf(.06f,pet.lid),1,motion_weight)-y;
        }
        uint8_t *bitmap=masks+BODY_BYTES+eye*EYE_SIZE*EYE_SIZE;
        lv_image_cache_drop(&images[eye+1]);raster(bitmap,EYE_SIZE,EYE_SIZE,ring,48);
        float alpha=mix(1,scene.eye_opa[eye],motion_weight);
        int left=EYE_SIZE,top=EYE_SIZE,right=-1,bottom=-1;
        // 使用当前身体遮罩裁切，变形过程中眼睛也不能逸出轮廓。
        // Clip eyes against the current body mask throughout silhouette morphs.
        for(int yy=0;yy<EYE_SIZE;++yy) for(int xx=0;xx<EYE_SIZE;++xx) {
            unsigned at=(unsigned)(yy*EYE_SIZE+xx);
            if(!bitmap[at])continue;
            if(x+xx<0||x+xx>=SCREEN||y+yy<0||y+yy>=SCREEN)bitmap[at]=0;
            else {
                uint8_t body_alpha=masks[(y+yy)*SCREEN+x+xx];
                if(!body_alpha)bitmap[at]=0;
                else if(body_alpha!=255||alpha!=1)bitmap[at]=(uint8_t)lroundf(bitmap[at]*(body_alpha/255.f)*alpha);
            }
            if(bitmap[at]) { left=LV_MIN(left,xx);right=LV_MAX(right,xx);top=LV_MIN(top,yy);bottom=LV_MAX(bottom,yy); }
        }
        if(right<left)left=top=right=bottom=0;
        // 图像只引用有效眼睛边界，旧位置先失效；保留原208字节行跨度与所有非零AA像素。
        // Reference only the eye's ink bounds after invalidating its old area; retain the 208-byte stride and every AA pixel.
        lv_obj_invalidate(eyes[eye]);
        images[eye+1].header.w=(uint32_t)(right-left+1);images[eye+1].header.h=(uint32_t)(bottom-top+1);
        images[eye+1].data=bitmap+top*EYE_SIZE+left;
        images[eye+1].data_size=(uint32_t)(EYE_SIZE*EYE_SIZE-top*EYE_SIZE-left);
        lv_image_set_src(eyes[eye],&images[eye+1]);
        lv_obj_set_pos(eyes[eye],x+left,y+top);lv_obj_invalidate(eyes[eye]);
    }
    prepare_decor();
    // 装饰淡出最后一帧也要失效，避免上一动作的彩环残留。
    // Invalidate the final faded frame as well so no prior orbit is left behind.
    if(motion_weight>0||decor_was_active) { lv_obj_invalidate(decor[0]);lv_obj_invalidate(decor[1]); }
    decor_was_active=motion_weight>0;
}
static void return_home(lv_event_t *event) { (void)event;go_home(); }
static void touch(lv_event_t *event) {
    if(!visible||!masks||lv_event_get_target_obj(event)!=root)return;
    lv_event_code_t code=lv_event_get_code(event);
    if(code==LV_EVENT_PRESS_LOST) { pressed=false;mimi_pet_touch(&pet,0,0,false);return; }
    if(code==LV_EVENT_RELEASED) {
        if(pressed&&!consumed)mimi_pet_react(&pet,stroke_distance>=24);
        pressed=false;mimi_pet_touch(&pet,0,0,false);return;
    }
    if(code!=LV_EVENT_PRESSED&&code!=LV_EVENT_PRESSING)return;
    lv_indev_t *input=lv_indev_active();if(!input)return;
    lv_point_t point;lv_indev_get_point(input,&point);
    if(code==LV_EVENT_PRESSED) { pressed=true;consumed=false;stroke_distance=0;previous=point;pressed_at=lv_tick_get(); }
    if(!pressed)return;
    stroke_distance+=hypotf(point.x-previous.x,point.y-previous.y);previous=point;
    lv_area_t bounds;lv_obj_get_coords(root,&bounds);
    mimi_pet_touch(&pet,(point.x-bounds.x1-232.5f)/233,(point.y-bounds.y1-232.5f)/233,true);
}
static void mimi_tick(void) {
    if(!root||!visible||!masks)return;
    uint32_t now=lv_tick_get(),elapsed=now-last_tick;
    if(elapsed<33){
#if MIMI_ACCELERATED
        // 20ms轮询只发布已完成彩环帧；宠物时钟、触摸和长按仍由原33ms门槛推进。
        // The20ms poll only publishes completed ring frames; the original33ms gate still advances pet/touch/hold state.
        bool rings=pet.motion_active&&(pet.motion==MIMI_MOTION_PLAY||pet.motion==MIMI_MOTION_ORBIT||pet.motion==MIMI_MOTION_COMET);
        if(rings&&accel_pipeline&&accel_requesting&&!accel_disabled){
            if(!accel_render()&&accel_disabled)render_pet();
        }
#endif
        return;
    }
    last_tick=now;
    if(pressed&&!consumed&&stroke_distance<12&&now-pressed_at>=900) {
        consumed=true;return_at=now;ui_obj_set_hidden(return_button,false);
    }
    if(!ui_obj_is_hidden(return_button)&&now-return_at>=3500)ui_obj_set_hidden(return_button,true);
    mimi_pet_step(&pet,elapsed);render_pet();
}
static void mimi_visibility(bool show) {
#if MIMI_ACCELERATED
    if(accel_pipeline)mimi_pipeline_invalidate(accel_pipeline);
    accel_requesting=false;
#endif
    visible=show;pressed=false;mimi_pet_touch(&pet,0,0,false);last_tick=lv_tick_get();
    if(return_button)ui_obj_set_hidden(return_button,true);
}
static void mimi_exit(void) {
    visible=pressed=false;
#if MIMI_ACCELERATED
    // 先撤掉LVGL引用，再等待后台退出；超时的pipeline继续拥有FILE和缓冲，不能提前释放。
    // Detach LVGL references before joining; a timed-out pipeline retains its FILE and buffers instead of freeing live storage.
    if(accel_image)lv_obj_delete(accel_image);
    accel_image=NULL;
    if(accel_renderer)mimi_render_attach(accel_renderer,NULL);
    lv_image_cache_drop(&accel_descriptor);memset(&accel_descriptor,0,sizeof accel_descriptor);
    heap_caps_free(accel_renderer);accel_renderer=NULL;accel_published=NULL;
    if(accel_pipeline){
        if(mimi_pipeline_destroy(accel_pipeline,&accel_exit_stats))accel_pipeline=NULL;
        else accel_warning("worker shutdown timed out");
    }
    accel_show=accel_requesting=false;
#endif
    for(unsigned i=0;i<2;++i) { if(eyes[i])lv_obj_delete(eyes[i]);eyes[i]=NULL; }
    if(body)lv_obj_delete(body);
    for(unsigned i=0;i<3;++i) { lv_image_cache_drop(&images[i]);memset(&images[i],0,sizeof images[i]); }
    heap_caps_free(masks);masks=NULL;decor_cache=NULL;
    root=body=return_button=error_label=NULL;decor[0]=decor[1]=NULL;body_ready=decor_was_active=false;
    launcher_set_app_fullscreen(false);
}
static void deleted(lv_event_t *event) { if(lv_event_get_target_obj(event)==root)mimi_exit(); }
static lv_obj_t *image(lv_obj_t *parent,lv_image_dsc_t *descriptor,int size,uint8_t *pixels,uint32_t color) {
    *descriptor=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_A8,.w=size,.h=size,.stride=size},
        .data=pixels,.data_size=(uint32_t)(size*size)};
    lv_obj_t *obj=lv_image_create(parent);lv_image_set_src(obj,descriptor);
    lv_obj_set_style_image_recolor(obj,lv_color_hex(color),0);lv_obj_set_style_image_recolor_opa(obj,LV_OPA_COVER,0);
    ui_obj_set_clickable(obj,false);return obj;
}
static void mimi_enter(lv_obj_t *parent) {
#if MIMI_ACCELERATED
    accel_disabled=false;accel_clip=UINT32_MAX;accel_show=accel_requesting=false;accel_published=NULL;
    if(accel_pipeline){
        if(mimi_pipeline_destroy(accel_pipeline,&accel_exit_stats))accel_pipeline=NULL;
        else {accel_disabled=true;accel_warning("previous worker still owns resources");}
    }
#endif
    mimi_pet_init(&pet);visible=true;pressed=consumed=false;body_ready=false;last_tick=lv_tick_get();
    launcher_set_app_fullscreen(true);
    root=tools_surface(parent,0,0,SCREEN,SCREEN);ui_obj_set_clickable(root,true);ui_obj_set_gesture_bubble(root,false);
    lv_obj_add_event_cb(root,deleted,LV_EVENT_DELETE,NULL);lv_obj_add_event_cb(root,touch,LV_EVENT_ALL,NULL);
    masks=heap_caps_malloc(MASK_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(masks) {
        memset(masks,0,MASK_BYTES);
        decor_cache=(mimi_decor_cache_t *)(void *)(masks+PIXEL_BYTES);
        decor[0]=tools_surface(root,0,0,SCREEN,SCREEN);
        lv_obj_add_event_cb(decor[0],draw_decor,LV_EVENT_DRAW_MAIN,(void *)(uintptr_t)0);
        body=image(root,&images[0],SCREEN,masks,TOOLS_WHITE);
        for(unsigned i=0;i<2;++i)eyes[i]=image(root,&images[i+1],EYE_SIZE,masks+BODY_BYTES+i*EYE_SIZE*EYE_SIZE,COL_BG);
        decor[1]=tools_surface(root,0,0,SCREEN,SCREEN);
        lv_obj_add_event_cb(decor[1],draw_decor,LV_EVENT_DRAW_MAIN,(void *)(uintptr_t)1);
#if MIMI_ACCELERATED
        if(!accel_disabled&&resource_store_mount()){
            accel_renderer=heap_caps_malloc(mimi_render_bytes(),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
            if(accel_renderer){
                mimi_render_init(accel_renderer);accel_pipeline=mimi_pipeline_create(MIMI_ASSET_PATH);
                if(!accel_pipeline){heap_caps_free(accel_renderer);accel_renderer=NULL;accel_warning("optimized assets unavailable");}
                else {
                    accel_descriptor=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=SCREEN,.h=SCREEN,.stride=SCREEN*2},.data=(const uint8_t*)mimi_render_pixels(accel_renderer),.data_size=SCREEN*SCREEN*2};
                    accel_image=lv_image_create(root);lv_image_set_src(accel_image,&accel_descriptor);
                    ui_obj_set_clickable(accel_image,false);ui_obj_set_hidden(accel_image,true);
                }
            } else accel_warning("no memory for optimized renderer");
        } else if(!accel_disabled)accel_warning("resource volume unavailable");
#endif
    } else {
        error_label=tools_label(root,settings_lang()?"内存不足":"Not enough memory",&font_mimi_20,233,233,TOOLS_GRAY,0);
    }
    // 长按显示临时返回键；抚摸不触发系统右滑退出，满屏状态不常驻按钮。
    // Holding reveals a temporary back control; strokes never trigger swipe-to-exit.
    return_button=lv_button_create(root);lv_obj_set_pos(return_button,109,40);lv_obj_set_size(return_button,48,48);
    lv_obj_set_style_radius(return_button,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_color(return_button,lv_color_hex(0x222226),0);
    ui_obj_set_event_bubble(return_button,false);ui_obj_set_gesture_bubble(return_button,false);
    lv_obj_add_event_cb(return_button,return_home,LV_EVENT_CLICKED,NULL);
    lv_obj_t *arrow=lv_label_create(return_button);lv_label_set_text(arrow,LV_SYMBOL_LEFT);lv_obj_center(arrow);
    ui_obj_set_hidden(return_button,masks!=NULL);
    render_pet();
}
const app_t app_mimi={.name="Mimi",.color=COL_TXT,.enter=mimi_enter,.tick=mimi_tick,
    .exit=mimi_exit,.tick_period_ms=20,.visibility=mimi_visibility};
