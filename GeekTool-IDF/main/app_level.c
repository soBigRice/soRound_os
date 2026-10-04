/*
 * @Author: superRice
 * @Date: 2026-06-22 12:20:25
 * @LastEditors: superRice 1246333567@qq.com
 * @LastEditTime: 2026-06-22 13:11:40
 * @FilePath: /wxesp32/GeekTool-IDF/main/app_level.c
 * @Description: 
 * Do your best to be yourself
 * Copyright (c) 2026 by superRice, All Rights Reserved. 
 */
// 水平仪:原来的倾角投影 / 时间消抖,与测量文字分离的靶盘。
#include "app.h"
#include "tools_ui.h"
#include "imu.h"
#include "ui_update.h"
#include "settings.h"
#include <math.h>
#include <stdio.h>

#define LCX 233
#define LCY 210
#define DISH 100
#define BALL_R 14
#define MAXR (DISH - 19)
#define TICK_OUTSET 13
// Keep the former 500px / 168px saturation ratio; resizing doesn't change the tilt response.
#define GAIN (500.0f * MAXR / 168.0f)
#define SMOOTH 0.35f
#define TARGET_TOP (LCY-DISH-TICK_OUTSET-1)
#define TARGET_BOTTOM (LCY+DISH+TICK_OUTSET+1)

static lv_obj_t *g_ball, *g_big, *g_unit, *g_status, *g_x, *g_y;
static lv_obj_t *g_content, *g_target, *g_fault, *g_origin;
static lv_point_precise_t s_vector[2];
static lv_obj_t *g_vector;
static float ox, oy;
static bool s_flat;
static uint32_t s_last_ms;
static unsigned s_missed;

static void target_draw(lv_event_t *e) {
    lv_area_t a; lv_obj_get_coords(g_target,&a);
    int cx=a.x1+114, cy=a.y1+114;
    lv_layer_t *layer=lv_event_get_layer(e);
    for (int i=0;i<72;++i) {
        float angle=i*(6.2831853f/72);
        tools_dot(layer,cx+(int)roundf(cosf(angle)*DISH),cy+(int)roundf(sinf(angle)*DISH),
                  i%6==0 ? 5 : 3,i%6==0 ? TOOLS_GRAY : TOOLS_LINE);
    }
    tools_circle(layer,cx,cy,64,1,TOOLS_FAINT);
    tools_circle(layer,cx,cy,32,1,TOOLS_FAINT);
    const int dx[]={1,0,-1,0},dy[]={0,1,0,-1};
    for (int i=0;i<4;++i) {
        tools_line(layer,cx+dx[i]*25,cy+dy[i]*25,cx+dx[i]*88,cy+dy[i]*88,1,TOOLS_LINE);
        tools_line(layer,cx+dx[i]*105,cy+dy[i]*105,cx+dx[i]*113,cy+dy[i]*113,2,TOOLS_GRAY);
    }
    tools_circle(layer,cx,cy,21,2,s_flat ? TOOLS_WHITE : TOOLS_GRAY);
    tools_dot(layer,cx,cy,4,TOOLS_GRAY);
}

static void origin_draw(lv_event_t *e) {
    lv_area_t a;lv_obj_get_coords(lv_event_get_target_obj(e),&a);
    // The approved center ring sits above the direction line, so its outline stays continuous.
    tools_circle(lv_event_get_layer(e),a.x1+7,a.y1+7,6,1,TOOLS_WHITE);
}

static void angle_layout(void) {
    lv_obj_update_layout(g_big);lv_obj_update_layout(g_unit);
    int number_width=lv_obj_get_width(g_big), unit_width=lv_obj_get_width(g_unit);
    int left=233-(number_width+7+unit_width)/2;
    tools_label_baseline(g_big,left,401);
    tools_label_baseline(g_unit,left+number_width+7,379);
}

static void level_tick(void);

static void level_enter(lv_obj_t *parent) {
    g_content=tools_surface(parent,0,0,466,466);
    // Includes the outward tick caps, not only the dotted circle. Text starts below this envelope.
    g_target=tools_surface(g_content,LCX-114,TARGET_TOP,229,TARGET_BOTTOM-TARGET_TOP+1);
    s_flat=true;
    lv_obj_add_event_cb(g_target,target_draw,LV_EVENT_DRAW_MAIN,NULL);
    g_vector=lv_line_create(g_content);
    lv_obj_set_style_line_width(g_vector,1,0);
    lv_obj_set_style_line_color(g_vector,lv_color_hex(TOOLS_DIM),0);
    ui_obj_set_event_bubble(g_vector,true);
    s_vector[0]=(lv_point_precise_t){LCX,LCY};s_vector[1]=s_vector[0];
    lv_line_set_points(g_vector,s_vector,2);
    g_origin=tools_surface(g_content,LCX-7,LCY-7,15,15);
    lv_obj_add_event_cb(g_origin,origin_draw,LV_EVENT_DRAW_MAIN,NULL);
    ui_obj_set_hidden(g_origin,true);
    g_ball=tools_surface(g_content,LCX-BALL_R,LCY-BALL_R,BALL_R*2,BALL_R*2);
    lv_obj_set_style_radius(g_ball,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_bg_color(g_ball,lv_color_hex(TOOLS_WHITE),0);
    lv_obj_set_style_bg_opa(g_ball,LV_OPA_COVER,0);
    lv_obj_t *hole=tools_surface(g_ball,BALL_R-3,BALL_R-3,6,6);
    lv_obj_set_style_radius(hole,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_bg_color(hole,lv_color_hex(COL_BG),0);
    lv_obj_set_style_bg_opa(hole,LV_OPA_COVER,0);
    g_status=tools_label(g_content,tools_text("LEVEL","已水平"),&font_tools_20,233,341,TOOLS_WHITE,settings_lang()?1:2);
    g_big=tools_label(g_content,"0",&font_tools_60,233,380,TOOLS_WHITE,0);
    g_unit=tools_label(g_content,"°",&font_tools_24,233,375,TOOLS_WHITE,0);
    angle_layout();
    tools_label(g_content,"X",&font_tools_19,164,427,TOOLS_GRAY,1);
    g_x=tools_label(g_content,"+0°",&font_tools_21,199,427,TOOLS_WHITE,0);
    lv_obj_t *divider=tools_surface(g_content,233,418,1,19);
    lv_obj_set_style_bg_color(divider,lv_color_hex(TOOLS_LINE),0);
    lv_obj_set_style_bg_opa(divider,LV_OPA_COVER,0);
    tools_label(g_content,"Y",&font_tools_19,264,427,TOOLS_GRAY,1);
    g_y=tools_label(g_content,"+0°",&font_tools_21,300,427,TOOLS_WHITE,0);
    g_fault=tools_fault(parent,false);
    ox=oy=0;s_last_ms=lv_tick_get();s_missed=0;
    // Don't present 0° until a real sample is available, even if init succeeds.
    ui_obj_set_hidden(g_content,true);
    ui_obj_set_hidden(g_ball,true);
    ui_obj_set_hidden(g_fault,false);
    imu_init();
    level_tick();
}

static void level_tick(void) {
    if (!g_ball) return;
    uint32_t now=lv_tick_get();
    float dt=fminf((float)(now-s_last_ms),100.0f);s_last_ms=now;
    float alpha=1.0f-powf(1.0f-SMOOTH,dt/50.0f);
    float tx,ty,az;
    if (!imu_read_tilt_z(&tx,&ty,&az) || !isfinite(tx) || !isfinite(ty) || !isfinite(az)) {
        if (++s_missed>=3) {
            ui_obj_set_hidden(g_content,true);
            ui_obj_set_hidden(g_ball,true);
            ui_obj_set_hidden(g_fault,false);
        }
        return;
    }
    s_missed=0;
    ui_obj_set_hidden(g_content,false);ui_obj_set_hidden(g_ball,false);
    ui_obj_set_hidden(g_fault,true);
    float gx=tx*GAIN,gy=ty*GAIN,d=sqrtf(gx*gx+gy*gy);
    if (d>MAXR) {gx=gx*MAXR/d;gy=gy*MAXR/d;}
    ox+=(gx-ox)*alpha;oy+=(gy-oy)*alpha;
    lv_obj_set_pos(g_ball,LCX+(int)ox-BALL_R,LCY+(int)oy-BALL_R);
    lv_point_precise_t endpoint={LCX+(int)ox,LCY+(int)oy};
    if (endpoint.x!=s_vector[1].x || endpoint.y!=s_vector[1].y) {
        s_vector[1]=endpoint;lv_line_set_points(g_vector,s_vector,2);
    }
    float tilt=sqrtf(tx*tx+ty*ty);
    ui_bg_color(g_ball,tilt<.12f ? TOOLS_WHITE : COL_RED);
    float azc=fabsf(az)<.05f ? .05f : fabsf(az);
    int total=(int)(asinf(tilt>1 ? 1 : tilt)*57.2958f+.5f);
    bool flat=total==0;
    if (flat!=s_flat) {
        s_flat=flat;lv_obj_invalidate(g_target);
        ui_text(g_status,flat ? tools_text("LEVEL","已水平") : tools_text("TILTED","偏离水平"));
        lv_obj_set_style_text_color(g_status,lv_color_hex(flat ? TOOLS_WHITE : TOOLS_GRAY),0);
        tools_label_center(g_status,233,341);
    }
    if (flat) {ui_obj_set_hidden(g_vector,true);ui_obj_set_hidden(g_origin,true);}
    else {ui_obj_set_hidden(g_vector,false);ui_obj_set_hidden(g_origin,false);}
    char number[8];snprintf(number,sizeof number,"%d",total);
    if (strcmp(lv_label_get_text(g_big),number)) {ui_text(g_big,number);angle_layout();}
    char axis[16];snprintf(axis,sizeof axis,"%+d°",(int)(atan2f(tx,azc)*57.2958f));
    if (strcmp(lv_label_get_text(g_x),axis)) {ui_text(g_x,axis);tools_label_center(g_x,199,427);}
    snprintf(axis,sizeof axis,"%+d°",(int)(atan2f(ty,azc)*57.2958f));
    if (strcmp(lv_label_get_text(g_y),axis)) {ui_text(g_y,axis);tools_label_center(g_y,300,427);}
}

static void level_exit(void) {
    g_ball=g_big=g_unit=g_status=g_x=g_y=g_content=g_target=g_fault=g_vector=g_origin=NULL;
}
static void level_visibility(bool visible) {(void)visible;s_last_ms=lv_tick_get();}
const app_t app_level={"level",COL_TXT,level_enter,level_tick,level_exit,NULL,20,level_visibility};
