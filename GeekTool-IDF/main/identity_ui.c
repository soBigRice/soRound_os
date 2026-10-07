#include "identity_ui.h"
#include "identity_geometry.h"
#include "src/core/lv_obj_private.h"
#include "src/core/lv_obj_class_private.h"
#include <math.h>

typedef struct {lv_obj_t obj; uint32_t elapsed_ms;} logo_t;
typedef struct {lv_obj_t obj; lv_obj_t *logo,*name;} boot_t;

static int32_t phase(uint32_t ms, uint32_t start, uint32_t duration) {
    if(ms <= start)return 0;
    if(ms >= start+duration)return LV_BEZIER_VAL_MAX;
    return (int32_t)((ms-start)*LV_BEZIER_VAL_MAX/duration);
}
static int32_t eased(int32_t value) {
    return lv_cubic_bezier(value,LV_BEZIER_VAL_FLOAT(.22f),LV_BEZIER_VAL_FLOAT(.61f),
                          LV_BEZIER_VAL_FLOAT(.36f),LV_BEZIER_VAL_MAX);
}
static void logo_draw(lv_obj_t *obj,lv_layer_t *layer) {
    logo_t *logo=(logo_t *)obj;
    lv_area_t area;lv_obj_get_coords(obj,&area);
    float scale=(float)lv_obj_get_width(obj)/IDENTITY_VIEWBOX;
    int cx=area.x1+(int)lroundf(IDENTITY_RING_CX*scale);
    int cy=area.y1+(int)lroundf(IDENTITY_RING_CY*scale);
    lv_draw_line_dsc_t style;lv_draw_line_dsc_init(&style);
    lv_obj_init_draw_line_dsc(obj,LV_PART_MAIN,&style);
    int32_t sweep=360*eased(phase(logo->elapsed_ms,160,740))/LV_BEZIER_VAL_MAX;
    if(sweep) {
        lv_draw_arc_dsc_t ring;lv_draw_arc_dsc_init(&ring);
        ring.base.obj=obj;ring.opa=style.opa;ring.color=lv_color_hex(IDENTITY_WHITE);
        // LVGL arc radius is the outer edge, while the SVG radius is its centerline.
        ring.center=(lv_point_t){cx,cy};ring.radius=(uint16_t)lroundf(IDENTITY_OUTER_RADIUS*scale);
        ring.width=(uint16_t)lroundf(IDENTITY_STROKE*scale);
        ring.start_angle=45;ring.end_angle=45+sweep;ring.rounded=true;
        lv_draw_arc(layer,&ring);
    }
    int32_t alpha=phase(logo->elapsed_ms,80,160);
    if(!alpha)return;
    float angle=(1.f-(float)eased(phase(logo->elapsed_ms,120,880))/LV_BEZIER_VAL_MAX)*
                1.57079632679489661923f;
    float dx=IDENTITY_DOT_CX-IDENTITY_RING_CX,dy=IDENTITY_DOT_CY-IDENTITY_RING_CY;
    int x=area.x1+(int)lroundf((IDENTITY_RING_CX+dx*cosf(angle)-dy*sinf(angle))*scale);
    int y=area.y1+(int)lroundf((IDENTITY_RING_CY+dx*sinf(angle)+dy*cosf(angle))*scale);
    int diameter=(int)lroundf(2.f*IDENTITY_DOT_RADIUS*scale);
    lv_draw_rect_dsc_t dot;lv_draw_rect_dsc_init(&dot);
    dot.base.obj=obj;dot.bg_opa=(lv_opa_t)((uint32_t)style.opa*alpha/LV_BEZIER_VAL_MAX);
    dot.bg_color=lv_color_hex(IDENTITY_RED);dot.radius=LV_RADIUS_CIRCLE;
    lv_area_t bounds={x-diameter/2,y-diameter/2,x-diameter/2+diameter-1,y-diameter/2+diameter-1};
    lv_draw_rect(layer,&dot,&bounds);
}
static void logo_event(const lv_obj_class_t *cls,lv_event_t *event) {
    lv_event_code_t code=lv_event_get_code(event);
    if(code==LV_EVENT_COVER_CHECK){lv_event_set_cover_res(event,LV_COVER_RES_NOT_COVER);return;}
    if(code==LV_EVENT_DRAW_MAIN){logo_draw(lv_event_get_target_obj(event),lv_event_get_layer(event));return;}
    if(code!=LV_EVENT_DRAW_MAIN_END)lv_obj_event_base(cls,event);
}
static const lv_obj_class_t logo_class={
    .base_class=&lv_obj_class,.event_cb=logo_event,.instance_size=sizeof(logo_t),.name="soRound_logo"
};
static const lv_obj_class_t boot_class={
    .base_class=&lv_obj_class,.instance_size=sizeof(boot_t),.name="soRound_boot"
};
lv_obj_t *identity_logo_create(lv_obj_t *parent,int size) {
    if(size<=0)return NULL;
    lv_obj_t *obj=lv_obj_class_create_obj(&logo_class,parent);if(!obj)return NULL;
    lv_obj_class_init_obj(obj);lv_obj_remove_style_all(obj);lv_obj_set_size(obj,size,size);
    lv_obj_remove_flag(obj,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(obj,LV_OBJ_FLAG_GESTURE_BUBBLE);
    ((logo_t *)obj)->elapsed_ms=1000;
    return obj;
}
static void boot_advance(void *var,int32_t ms) {
    boot_t *boot=var;logo_t *logo=(logo_t *)boot->logo;
    // Only the central mark changes before 1s; don't invalidate the full black screen.
    uint32_t logo_ms=(uint32_t)(ms<1000?ms:1000);
    if(logo->elapsed_ms!=logo_ms){logo->elapsed_ms=logo_ms;lv_obj_invalidate(boot->logo);}
    int32_t fade=phase((uint32_t)ms,1020,280);
    lv_obj_set_style_text_opa(boot->name,(lv_opa_t)(255*fade/LV_BEZIER_VAL_MAX),0);
    int baseline=349-7*eased(fade)/LV_BEZIER_VAL_MAX;
    lv_obj_set_y(boot->name,baseline-font_identity_26.line_height+font_identity_26.base_line);
}
lv_obj_t *identity_boot_create(lv_obj_t *parent) {
    lv_obj_t *obj=lv_obj_class_create_obj(&boot_class,parent);if(!obj)return NULL;
    lv_obj_class_init_obj(obj);lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj,466,466);lv_obj_set_pos(obj,0,0);
    lv_obj_set_style_bg_color(obj,lv_color_black(),0);lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);
    // Swallow touches/gestures while the original lockscreen continues underneath.
    lv_obj_remove_flag(obj,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_GESTURE_BUBBLE|LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(obj,LV_OBJ_FLAG_CLICKABLE);
    boot_t *boot=(boot_t *)obj;
    boot->logo=identity_logo_create(obj,160);boot->name=lv_label_create(obj);
    if(!boot->logo||!boot->name){lv_obj_delete(obj);return NULL;}
    lv_obj_set_pos(boot->logo,153,153);
    lv_obj_remove_flag(boot->logo,LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_remove_flag(boot->name,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_label_set_text(boot->name,"soRound OS");lv_obj_set_width(boot->name,300);lv_obj_set_x(boot->name,83);
    lv_obj_set_style_text_font(boot->name,&font_identity_26,0);
    lv_obj_set_style_text_align(boot->name,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_color(boot->name,lv_color_hex(IDENTITY_WHITE),0);
    lv_anim_t animation;lv_anim_init(&animation);lv_anim_set_var(&animation,obj);
    lv_anim_set_exec_cb(&animation,boot_advance);lv_anim_set_values(&animation,0,IDENTITY_BOOT_MS);
    lv_anim_set_duration(&animation,IDENTITY_BOOT_MS);
    // LVGL deletes animations attached to the object, including an early parent deletion.
    lv_anim_set_completed_cb(&animation,lv_obj_delete_anim_completed_cb);
    if(!lv_anim_start(&animation)){lv_obj_delete(obj);return NULL;}
    return obj;
}
