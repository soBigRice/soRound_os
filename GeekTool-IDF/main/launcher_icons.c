// 已选定A套：148px透明画布，离线校准的实心轮廓。
// Approved direction A: a 148px transparent canvas with optically aligned solid silhouettes.
#include "launcher_icons.h"
#include "app.h"
#include "src/core/lv_obj_private.h"
#include "src/core/lv_obj_class_private.h"
#include "launcher_artwork.inc"

#define INK 0xf5f5f2

typedef struct { lv_obj_t obj; launcher_icon_t kind; } icon_t;
typedef struct { lv_obj_t *obj; lv_layer_t *layer; int x,y; lv_opa_t opa; } ink_t;

static void line(ink_t *p,int x1,int y1,int x2,int y2) {
    lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);
    d.base.obj=p->obj;d.opa=p->opa;d.width=3;d.color=lv_color_hex(COL_TXT2);
    d.round_start=d.round_end=true;
    d.p1=(lv_point_precise_t){p->x+x1,p->y+y1};
    d.p2=(lv_point_precise_t){p->x+x2,p->y+y2};
    lv_draw_line(p->layer,&d);
}

static void draw_mask(ink_t *p,const lv_image_dsc_t *mask,uint32_t color) {
    if(!mask)return;
    lv_draw_image_dsc_t d;lv_draw_image_dsc_init(&d);
    d.base.obj=p->obj;d.src=mask;d.opa=p->opa;d.recolor=lv_color_hex(color);
    // 使用控件的实际屏幕原点；父层平移和切换层共用同一坐标系。
    // Use the widget's actual screen origin, shared by parent translation and swap layers.
    lv_area_t bounds={p->x,p->y,p->x+LAUNCHER_ICON_SIZE-1,p->y+LAUNCHER_ICON_SIZE-1};
    lv_draw_image(p->layer,&d,&bounds);
}

static void paint(ink_t *p,launcher_icon_t kind) {
    if((unsigned)kind<LAUNCHER_ICON_COUNT) {
        draw_mask(p,launcher_artwork[kind].white,INK);
        draw_mask(p,launcher_artwork[kind].red,COL_RED);
    } else if(kind==LAUNCHER_PREV) {
        line(p,16,6,8,14);line(p,8,14,16,22);
    } else if(kind==LAUNCHER_NEXT) {
        line(p,12,6,20,14);line(p,20,14,12,22);
    }
}
static void icon_event(const lv_obj_class_t *cls,lv_event_t *e) {
    lv_event_code_t code=lv_event_get_code(e);
    if(code==LV_EVENT_COVER_CHECK) {lv_event_set_cover_res(e,LV_COVER_RES_NOT_COVER);return;}
    if(code!=LV_EVENT_DRAW_MAIN && code!=LV_EVENT_DRAW_MAIN_END) {lv_obj_event_base(cls,e);return;}
    if(code!=LV_EVENT_DRAW_MAIN)return;
    lv_obj_t *obj=lv_event_get_target_obj(e);lv_area_t a;lv_obj_get_coords(obj,&a);
    // 保留现有父层整体透明度，避免红白交界在切换时二次变亮。
    // Preserve parent-layer opacity so white/red joins do not brighten during the swap.
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
    lv_obj_set_style_line_width(obj,3,0);
    ui_obj_set_scrollable(obj,false);ui_obj_set_clickable(obj,false);ui_obj_set_event_bubble(obj,true);
    ((icon_t *)obj)->kind=kind;return obj;
}
void launcher_icon_set(lv_obj_t *obj,launcher_icon_t kind) {
    if(!obj || (unsigned)kind>=LAUNCHER_ICON_COUNT)return;
    icon_t *icon=(icon_t *)obj;if(icon->kind==kind)return;
    icon->kind=kind;lv_obj_invalidate(obj);
}
