// Optical grid: 148px canvas, 7px round strokes, off-white body and one red detail.
// Native LVGL primitives keep crisp edges without shipping raster assets or an SVG engine.
#include "launcher_icons.h"
#include "app.h"
#include "src/core/lv_obj_private.h"
#include "src/core/lv_obj_class_private.h"

#define INK 0xf5f5f2
#define STROKE 7

typedef struct { lv_obj_t obj; launcher_icon_t kind; } icon_t;
typedef struct { lv_obj_t *obj; lv_layer_t *layer; int x,y; lv_opa_t opa; } ink_t;

static void line(ink_t *p,int x1,int y1,int x2,int y2,int width,uint32_t color) {
    lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);
    d.base.obj=p->obj;d.opa=p->opa;d.width=width;d.color=lv_color_hex(color);
    d.round_start=d.round_end=true;
    d.p1=(lv_point_precise_t){p->x+x1,p->y+y1};d.p2=(lv_point_precise_t){p->x+x2,p->y+y2};
    lv_draw_line(p->layer,&d);
}
static void arc(ink_t *p,int x,int y,int r,int start,int end,int width,uint32_t color) {
    lv_draw_arc_dsc_t d;lv_draw_arc_dsc_init(&d);
    d.base.obj=p->obj;d.opa=p->opa;d.color=lv_color_hex(color);d.width=width;
    d.center=(lv_point_t){p->x+x,p->y+y};d.radius=r+(width+1)/2;
    d.start_angle=start;d.end_angle=end;d.rounded=true;lv_draw_arc(p->layer,&d);
}
static void dot(ink_t *p,int x,int y,int diameter,uint32_t color) {
    lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);
    d.base.obj=p->obj;d.bg_opa=p->opa;d.bg_color=lv_color_hex(color);d.radius=LV_RADIUS_CIRCLE;
    lv_area_t a={p->x+x-diameter/2,p->y+y-diameter/2,0,0};
    a.x2=a.x1+diameter-1;a.y2=a.y1+diameter-1;lv_draw_rect(p->layer,&d,&a);
}
static void box(ink_t *p,int x,int y,int w,int h,int radius,uint32_t color) {
    lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);
    d.base.obj=p->obj;d.bg_opa=LV_OPA_TRANSP;d.border_opa=p->opa;
    d.border_color=lv_color_hex(color);d.border_width=STROKE;d.radius=radius;
    lv_area_t a={p->x+x,p->y+y,p->x+x+w-1,p->y+y+h-1};lv_draw_rect(p->layer,&d,&a);
}
static void stroke(ink_t *p,int x1,int y1,int x2,int y2) {line(p,x1,y1,x2,y2,STROKE,INK);}
static void circle(ink_t *p,int x,int y,int r,uint32_t color) {arc(p,x,y,r,0,360,STROKE,color);}
static void cube(ink_t *p,int cx) {
    stroke(p,cx-25,55,cx,40);stroke(p,cx,40,cx+25,55);
    stroke(p,cx+25,55,cx+25,95);stroke(p,cx+25,95,cx,110);
    stroke(p,cx,110,cx-25,95);stroke(p,cx-25,95,cx-25,55);
    stroke(p,cx-25,55,cx,70);stroke(p,cx,70,cx+25,55);stroke(p,cx,70,cx,110);
}
static void paint(ink_t *p,launcher_icon_t kind) {
    switch(kind) {
    case LAUNCHER_WIFI:
        arc(p,74,101,60,210,330,STROKE,INK);arc(p,74,101,40,213,327,STROKE,INK);
        arc(p,74,101,20,220,320,STROKE,INK);dot(p,74,117,12,COL_RED);break;
    case LAUNCHER_SCAN:
        stroke(p,25,48,25,25);stroke(p,25,25,48,25);
        stroke(p,100,25,123,25);stroke(p,123,25,123,48);
        stroke(p,123,100,123,123);stroke(p,123,123,100,123);
        stroke(p,48,123,25,123);stroke(p,25,123,25,100);
        circle(p,68,67,21,INK);stroke(p,84,83,102,101);dot(p,68,67,9,COL_RED);break;
    case LAUNCHER_SYSTEM:
        box(p,38,38,72,72,13,INK);
        for(int k=-1;k<=1;++k) {
            stroke(p,74+k*18,19,74+k*18,32);stroke(p,74+k*18,116,74+k*18,129);
            stroke(p,19,74+k*18,32,74+k*18);stroke(p,116,74+k*18,129,74+k*18);
        }
        for(int r=0;r<2;++r)for(int c=0;c<2;++c)dot(p,68+c*13,68+r*13,8,COL_RED);
        break;
    case LAUNCHER_WEATHER:
        arc(p,99,56,20,185,360,STROKE,COL_RED);
        line(p,99,22,99,28,STROKE,COL_RED);line(p,126,30,131,25,STROKE,COL_RED);
        line(p,132,56,138,56,STROKE,COL_RED);
        // Analytic arcs avoid uneven stroke joins from short integer line segments.
        arc(p,40,98,20,90,270,STROKE,INK);arc(p,68,79,28,180,345,STROKE,INK);
        arc(p,104,94,24,247,90,STROKE,INK);stroke(p,104,118,40,118);break;
    case LAUNCHER_CALENDAR:
        box(p,23,29,102,102,16,INK);stroke(p,23,59,125,59);
        stroke(p,49,17,49,40);stroke(p,99,17,99,40);
        for(int r=0;r<2;++r)for(int c=0;c<3;++c)
            dot(p,47+c*27,82+r*24,(r==1 && c==2)?13:7,(r==1 && c==2)?COL_RED:INK);
        break;
    case LAUNCHER_COUNTDOWN:
        stroke(p,38,23,110,23);stroke(p,38,125,110,125);
        stroke(p,44,24,44,40);stroke(p,104,24,104,40);arc(p,74,40,30,0,180,STROKE,INK);
        stroke(p,74,70,74,79);arc(p,74,109,30,180,360,STROKE,INK);
        stroke(p,44,109,44,124);stroke(p,104,109,104,124);
        dot(p,74,93,6,COL_RED);dot(p,68,104,6,COL_RED);dot(p,80,104,6,COL_RED);
        for(int k=0;k<3;++k)dot(p,62+k*12,115,6,COL_RED);
        break;
    case LAUNCHER_STOPWATCH:
        circle(p,74,83,45,INK);stroke(p,61,16,87,16);stroke(p,74,20,74,31);
        stroke(p,112,32,121,41);stroke(p,74,44,74,50);
        stroke(p,109,83,114,83);stroke(p,74,116,74,122);stroke(p,35,83,41,83);
        line(p,74,83,97,60,STROKE,COL_RED);dot(p,74,83,9,COL_RED);break;
    case LAUNCHER_SETTINGS:
        stroke(p,19,39,31,39);stroke(p,65,39,129,39);circle(p,48,39,12,INK);
        stroke(p,19,74,83,74);stroke(p,117,74,129,74);circle(p,100,74,12,COL_RED);
        stroke(p,19,109,48,109);stroke(p,82,109,129,109);circle(p,65,109,12,INK);break;
    case LAUNCHER_OTA:
        stroke(p,74,35,74,96);line(p,49,60,74,35,STROKE,COL_RED);
        line(p,74,35,99,60,STROKE,COL_RED);
        stroke(p,28,96,28,117);arc(p,37,117,9,90,180,STROKE,INK);
        stroke(p,37,126,111,126);arc(p,111,117,9,0,90,STROKE,INK);stroke(p,120,117,120,96);break;
    case LAUNCHER_AUDIO: {
        static const int heights[]={28,58,88,112,76,52,26};
        for(int k=0;k<7;++k)line(p,25+k*16,74-heights[k]/2,25+k*16,74+heights[k]/2,
                                    STROKE,k==3?COL_RED:INK);
        break;
    }
    case LAUNCHER_LEVEL:
        box(p,15,44,118,61,22,INK);stroke(p,50,50,50,98);stroke(p,98,50,98,98);
        circle(p,74,74,12,COL_RED);break;
    case LAUNCHER_MAZE:
        stroke(p,23,73,23,23);stroke(p,23,23,125,23);stroke(p,125,23,125,125);
        stroke(p,125,125,23,125);stroke(p,23,125,23,102);
        stroke(p,23,55,79,55);stroke(p,79,23,79,55);stroke(p,79,55,79,89);
        stroke(p,54,89,105,89);stroke(p,54,89,54,125);dot(p,47,73,11,COL_RED);break;
    case LAUNCHER_FLUID:
        stroke(p,74,20,110,68);arc(p,74,94,44,324,216,STROKE,INK);stroke(p,38,68,74,20);
        dot(p,65,106,8,COL_RED);dot(p,79,114,8,COL_RED);dot(p,93,104,8,COL_RED);break;
    case LAUNCHER_DICE:
        box(p,22,22,104,104,22,INK);
        dot(p,49,49,11,INK);dot(p,99,49,11,INK);dot(p,49,99,11,INK);dot(p,99,99,11,INK);
        dot(p,74,74,12,COL_RED);break;
    case LAUNCHER_REMOTE:
        box(p,33,14,82,120,40,INK);stroke(p,38,59,110,59);stroke(p,74,17,74,29);
        line(p,74,37,74,47,STROKE,COL_RED);break;
    case LAUNCHER_TWIN:
        cube(p,40);cube(p,108);line(p,67,75,81,75,5,COL_RED);dot(p,74,75,8,COL_RED);break;
    case LAUNCHER_PREV:
        line(p,16,6,8,14,3,COL_TXT2);line(p,8,14,16,22,3,COL_TXT2);break;
    case LAUNCHER_NEXT:
        line(p,12,6,20,14,3,COL_TXT2);line(p,20,14,12,22,3,COL_TXT2);break;
    default: break;
    }
}
static void icon_event(const lv_obj_class_t *cls,lv_event_t *e) {
    lv_event_code_t code=lv_event_get_code(e);
    if(code==LV_EVENT_COVER_CHECK) {lv_event_set_cover_res(e,LV_COVER_RES_NOT_COVER);return;}
    if(code!=LV_EVENT_DRAW_MAIN && code!=LV_EVENT_DRAW_MAIN_END) {lv_obj_event_base(cls,e);return;}
    if(code!=LV_EVENT_DRAW_MAIN)return;
    lv_obj_t *obj=lv_event_get_target_obj(e);lv_area_t a;lv_obj_get_coords(obj,&a);
    // The initialized descriptor applies parent/layer opacity during the existing swap fade.
    lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);lv_obj_init_draw_line_dsc(obj,LV_PART_MAIN,&d);
    ink_t p={obj,lv_event_get_layer(e),a.x1,a.y1,d.opa};paint(&p,((icon_t *)obj)->kind);
}
static const lv_obj_class_t icon_class={
    .base_class=&lv_obj_class,.event_cb=icon_event,.instance_size=sizeof(icon_t),.name="launcher_icon"
};
lv_obj_t *launcher_icon_create(lv_obj_t *parent,launcher_icon_t kind) {
    if((unsigned)kind>LAUNCHER_NEXT)return NULL;
    lv_obj_t *obj=lv_obj_class_create_obj(&icon_class,parent);if(!obj)return NULL;
    lv_obj_class_init_obj(obj);lv_obj_remove_style_all(obj);
    int size=kind<LAUNCHER_ICON_COUNT?LAUNCHER_ICON_SIZE:28;lv_obj_set_size(obj,size,size);
    lv_obj_set_style_line_width(obj,STROKE,0);
    ui_obj_set_scrollable(obj,false);ui_obj_set_clickable(obj,false);ui_obj_set_event_bubble(obj,true);
    ((icon_t *)obj)->kind=kind;return obj;
}
void launcher_icon_set(lv_obj_t *obj,launcher_icon_t kind) {
    if(!obj || (unsigned)kind>=LAUNCHER_ICON_COUNT)return;
    icon_t *icon=(icon_t *)obj;if(icon->kind==kind)return;
    icon->kind=kind;lv_obj_invalidate(obj);
}
