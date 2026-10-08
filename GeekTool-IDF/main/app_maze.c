// Fixed, replayable levels; retain the continuous tilt-driven rolling and wall collision model.
#include "app.h"
#include "glyph.h"
#include "imu.h"
#include "esp_timer.h"
#include "ui_update.h"
#include "lvgl_compat.h"
#include "settings.h"
#include "watchface_ui.h"
#include "maze_levels.h"
#include <math.h>
#include <stdio.h>
#define MCX 233
#define MCY 233
#define WT 5
#define OX 95
#define OY 114
#define BOARD 276
#define BALL_R 8
#define BOUND_R 215
#define G_MS2 9.8f
#define PPM 150.0f
#define DT_S .05f
#define SUBS 5
#define BDAMP .997f
#define VMAX 560.0f
#define MAXW 100
typedef struct {int x,y,w,h;} rect_t;
typedef enum {MAZE_MENU,MAZE_PLAY,MAZE_WIN} maze_mode_t;
static rect_t s_walls[MAXW];
static int s_nwall,g_level;
static uint16_t g_completed;
static lv_obj_t *g_maze_parent,*g_maze_ui,*g_wallbox,*g_ball,*g_msg;
static maze_mode_t g_maze_mode;
static float bx,by,vx,vy;
static int64_t s_last_us;
static bool g_maze_queued;
static float clampf(float v,float lo,float hi){return v<lo?lo:v>hi?hi:v;}
static const char *maze_word(const char *en,const char *zh){return settings_lang()?zh:en;}
static const maze_level_t *current_level(void){return &maze_levels[g_level];}
static void maze_rebuild(void *arg);
static void maze_queue(void){if(!g_maze_queued){g_maze_queued=true;lv_async_call(maze_rebuild,NULL);}}
static lv_obj_t *maze_text(const char *value,int cy,const lv_font_t *font,uint32_t color){
    lv_obj_t *label=lv_label_create(g_maze_ui);lv_obj_set_style_text_font(label,font,0);lv_obj_set_style_text_color(label,lv_color_hex(color),0);
    lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);lv_label_set_text(label,value);lv_obj_align(label,LV_ALIGN_TOP_MID,0,cy-font->line_height/2);return label;
}
static void maze_choose(lv_event_t *e){g_level=(uintptr_t)lv_event_get_user_data(e);g_maze_mode=MAZE_PLAY;maze_queue();}
static void maze_retry(lv_event_t *e){(void)e;g_maze_mode=MAZE_PLAY;maze_queue();}
static void maze_next(lv_event_t *e){(void)e;if(g_level+1<MAZE_LEVEL_COUNT){++g_level;g_maze_mode=MAZE_PLAY;}else g_maze_mode=MAZE_MENU;maze_queue();}
static lv_obj_t *maze_button(int x,int y,int w,int h,const char *value,const lv_font_t *font,lv_event_cb_t cb,uintptr_t data){
    lv_obj_t *button=lv_button_create(g_maze_ui);lv_obj_remove_style_all(button);lv_obj_set_pos(button,x,y);lv_obj_set_size(button,w,h);
    lv_obj_set_style_radius(button,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_color(button,lv_color_hex(0x141418),0);lv_obj_set_style_bg_opa(button,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(button,1,0);lv_obj_set_style_border_color(button,lv_color_hex(0x45454b),0);
    ui_obj_set_scrollable(button,false);ui_obj_set_gesture_bubble(button,false);lv_obj_add_event_cb(button,cb,LV_EVENT_CLICKED,(void *)data);
    lv_obj_t *label=lv_label_create(button);lv_obj_set_style_text_font(label,font,0);lv_obj_set_style_text_color(label,lv_color_hex(COL_TXT),0);lv_label_set_text(label,value);lv_obj_center(label);return button;
}
static void add_wall(int x,int y,int w,int h){
    s_walls[s_nwall++]=(rect_t){x,y,w,h};
    lv_obj_t *o=lv_obj_create(g_wallbox);lv_obj_remove_style_all(o);lv_obj_set_size(o,w,h);lv_obj_set_pos(o,x,y);
    lv_obj_set_style_radius(o,2,0);lv_obj_set_style_bg_color(o,lv_color_hex(0x5f6069),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);ui_obj_set_event_bubble(o,true);
}
static void maze_build(void){
    const maze_level_t *level=current_level();int n=level->size;float cell=BOARD/(float)n;s_nwall=0;
    for(int r=0;r<n;++r)for(int c=0;c<n;++c){int x=(int)lroundf(OX+c*cell),y=(int)lroundf(OY+r*cell);
        int w=(int)lroundf(OX+(c+1)*cell)-x,h=(int)lroundf(OY+(r+1)*cell)-y;uint8_t walls=level->walls[r*n+c];
        if(walls&1)add_wall(x-WT/2,y-WT/2,w+WT,WT);
        if(walls&8)add_wall(x-WT/2,y-WT/2,WT,h+WT);
    }
    add_wall(OX-WT/2,OY+BOARD-WT/2,BOARD+WT,WT);add_wall(OX+BOARD-WT/2,OY-WT/2,WT,BOARD+WT);
    int gx=(int)lroundf(OX+BOARD-cell/2),gy=(int)lroundf(OY+BOARD-cell/2);
    glyph_circle(g_wallbox,gx,gy,13,11,2,COL_RED);glyph_dot(g_wallbox,gx,gy,4,COL_RED);
    bx=OX+cell/2;by=OY+cell/2;vx=vy=0;s_last_us=esp_timer_get_time();
}
static void maze_rebuild(void *arg){
    (void)arg;g_maze_queued=false;if(!g_maze_parent)return;
    if(g_maze_ui)lv_obj_delete(g_maze_ui);
    g_wallbox=g_ball=g_msg=NULL;
    g_maze_ui=lv_obj_create(g_maze_parent);lv_obj_remove_style_all(g_maze_ui);lv_obj_set_size(g_maze_ui,466,466);
    ui_obj_set_scrollable(g_maze_ui,false);ui_obj_set_event_bubble(g_maze_ui,true);
    if(g_maze_mode==MAZE_MENU){
        maze_text(maze_word("12 levels. Take your time.","十二关，慢慢走。"),107,UI_FONT_M,COL_TXT);
        const char *const en[]={"START / 4 x 4","TURN / 5 x 5","PRECISION / 6 x 6"};
        const char *const zh[]={"入门 / 4 x 4","转向 / 5 x 5","精密 / 6 x 6"};
        for(int r=0;r<3;++r){maze_text(settings_lang()?zh[r]:en[r],140+r*93,UI_FONT_M,COL_TXT2);
            for(int c=0;c<4;++c){int id=r*4+c,x=134+c*66,y=177+r*93;char value[4];snprintf(value,sizeof value,"%02d",id+1);
                lv_obj_t *b=maze_button(x-26,y-26,52,52,value,&font_hand_regular_24,maze_choose,id);
                if(id==g_level)lv_obj_set_style_border_color(b,lv_color_hex(COL_RED),0);
                if(g_completed&(1u<<id))glyph_dot(b,26,45,2,COL_RED);
            }
        }
        maze_text(maze_word("Fixed maps / replay anytime","固定地图 / 随时重玩"),415,UI_FONT_M,COL_TXT2);return;
    }
    const maze_level_t *level=current_level();char value[64];
    snprintf(value,sizeof value,"%02d / 12  %s",g_level+1,settings_lang()?level->name_zh:level->name_en);
    maze_text(value,g_maze_mode==MAZE_PLAY?94:115,UI_FONT_M,COL_TXT2);
    if(g_maze_mode==MAZE_WIN){
        for(int i=0;i<12;++i){float a=i*3.14159265f/6;glyph_dot(g_maze_ui,(int)lroundf(233+sinf(a)*71),(int)lroundf(225-cosf(a)*71),3,i<=g_level?COL_RED:0x25252b);}
        snprintf(value,sizeof value,"%02d",g_level+1);maze_text(value,213,&font_wf_72,COL_TXT);
        maze_text(maze_word("Complete","完成"),265,UI_FONT_M,COL_TXT2);
        maze_button(159,322,148,48,g_level==11?maze_word("Levels","返回关卡"):maze_word("Next","下一关"),UI_FONT_M,maze_next,0);
        maze_button(171,379,124,36,maze_word("Replay","重玩"),UI_FONT_M,maze_retry,0);return;
    }
    g_wallbox=lv_obj_create(g_maze_ui);lv_obj_remove_style_all(g_wallbox);lv_obj_set_size(g_wallbox,466,466);
    ui_obj_set_scrollable(g_wallbox,false);ui_obj_set_event_bubble(g_wallbox,true);maze_build();
    g_ball=lv_obj_create(g_maze_ui);lv_obj_remove_style_all(g_ball);lv_obj_set_size(g_ball,BALL_R*2,BALL_R*2);
    lv_obj_set_style_radius(g_ball,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_color(g_ball,lv_color_hex(COL_TXT),0);lv_obj_set_style_bg_opa(g_ball,LV_OPA_COVER,0);
    ui_obj_set_event_bubble(g_ball,true);lv_obj_set_pos(g_ball,(int)bx-BALL_R,(int)by-BALL_R);
    maze_button(311,40,48,48,LV_SYMBOL_REFRESH,UI_FONT_SYM,maze_retry,0);
    g_msg=maze_text(imu_init()?maze_word("Tilt into the red dot","倾斜设备，滚入红点"):tr(S_FLUID_NOIMU),417,UI_FONT_M,COL_TXT2);
}
static void maze_enter(lv_obj_t *parent){g_maze_parent=parent;g_maze_mode=MAZE_MENU;maze_rebuild(NULL);}
static void maze_tick(void){
    if(!g_ball||g_maze_mode!=MAZE_PLAY||g_maze_queued)return;
    int64_t now=esp_timer_get_time();float dt=fminf((now-s_last_us)/1000000.0f,.1f);s_last_us=now;
    float tx,ty;if(!imu_read_tilt(&tx,&ty)){lv_label_set_text(g_msg,tr(S_FLUID_NOIMU));return;}
    lv_label_set_text(g_msg,maze_word("Tilt into the red dot","倾斜设备，滚入红点"));
    float ax = tx * G_MS2 * PPM, ay = ty * G_MS2 * PPM;   // 真实重力加速度(px/s^2)
    int steps = (int)ceilf(dt / (DT_S / SUBS));
    if (steps < 1) return;
    float sdt = dt / steps, damping = powf(BDAMP, sdt / (DT_S / SUBS));
    for (int s = 0; s < steps; s++) {                       // 按时间分子步积分(球可跑很快也不穿墙)
        vx += ax * sdt; vy += ay * sdt;                    // vx/vy 单位:px/s
        vx *= damping; vy *= damping;
        float sp = sqrtf(vx * vx + vy * vy);
        if (sp > VMAX) { vx = vx * VMAX / sp; vy = vy * VMAX / sp; }
        float nx = bx + vx * sdt, ny = by + vy * sdt;
        for (int i = 0; i < s_nwall; i++) {               // 圆 vs 矩形
            float cx = clampf(nx, s_walls[i].x, s_walls[i].x + s_walls[i].w);
            float cy = clampf(ny, s_walls[i].y, s_walls[i].y + s_walls[i].h);
            float dx = nx - cx, dy = ny - cy, d2 = dx * dx + dy * dy;
            if (d2 < BALL_R * BALL_R) {
                float d = sqrtf(d2);
                if (d > 0.01f) {
                    float nxn = dx / d, nyn = dy / d;
                    nx = cx + nxn * BALL_R; ny = cy + nyn * BALL_R;
                    float vn = vx * nxn + vy * nyn;
                    if (vn < 0) { vx -= vn * nxn; vy -= vn * nyn; }
                } else { ny = s_walls[i].y - BALL_R; vy = 0; }
            }
        }
        float ex = nx - MCX, ey = ny - MCY, ed = sqrtf(ex * ex + ey * ey);
        if (ed > BOUND_R) { nx = MCX + ex / ed * BOUND_R; ny = MCY + ey / ed * BOUND_R; vx *= 0.3f; vy *= 0.3f; }
        bx = nx; by = ny;
    }
    lv_obj_set_pos(g_ball, (int)bx - BALL_R, (int)by - BALL_R);


    float cell=BOARD/(float)current_level()->size;
    if(hypotf(bx-(OX+BOARD-cell/2),by-(OY+BOARD-cell/2))<12){g_completed|=1u<<g_level;g_maze_mode=MAZE_WIN;vx=vy=0;maze_queue();}
}
static bool maze_back(void){if(g_maze_mode==MAZE_MENU)return false;g_maze_mode=MAZE_MENU;vx=vy=0;maze_queue();return true;}
static void maze_exit(void){if(g_maze_queued)lv_async_call_cancel(maze_rebuild,NULL);g_maze_queued=false;g_maze_parent=g_maze_ui=g_wallbox=g_ball=g_msg=NULL;}
static void maze_visibility(bool visible){(void)visible;s_last_us=esp_timer_get_time();}
const app_t app_maze={"maze",COL_TXT,maze_enter,maze_tick,maze_exit,maze_back,20,maze_visibility};
