#include "companion_icons.h"
#include "companion_icons.inc"

// 小咪静态入口与 App 共用白团黑眼睛的识别特征。
// The launcher keeps Mimi's white orb and slanted black eyes recognizable.
static void mimi_icon(lv_event_t *e) {
    lv_area_t a; lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_draw_rect_dsc_t circle; lv_draw_rect_dsc_init(&circle);
    circle.bg_color = lv_color_hex(0xf5f5f2); circle.bg_opa = LV_OPA_COVER;
    circle.radius = LV_RADIUS_CIRCLE;
    lv_area_t bounds = {a.x1+12, a.y1+12, a.x1+135, a.y1+135};
    lv_draw_rect(layer, &circle, &bounds);
    lv_draw_line_dsc_t eye; lv_draw_line_dsc_init(&eye);
    lv_obj_init_draw_line_dsc(lv_event_get_target_obj(e), LV_PART_MAIN, &eye);
    eye.color = lv_color_black(); eye.width = 12; eye.round_start = eye.round_end = true;
    eye.p1 = (lv_point_precise_t){a.x1+81,a.y1+42}; eye.p2 = (lv_point_precise_t){a.x1+87,a.y1+56};
    lv_draw_line(layer, &eye);
    eye.width = 10;
    eye.p1 = (lv_point_precise_t){a.x1+107,a.y1+37}; eye.p2 = (lv_point_precise_t){a.x1+112,a.y1+51};
    lv_draw_line(layer, &eye);
}

void companion_launcher_icon_set(lv_obj_t *icon, int index) {
    if (!icon || index < 0 || index >= LAUNCHER_ICON_COUNT + COMPANION_ICON_COUNT) return;
    lv_obj_t *original=lv_obj_get_child(icon,0), *image=lv_obj_get_child(icon,1);
    lv_obj_t *mimi=lv_obj_get_child(icon,2);
    bool is_mimi=index==LAUNCHER_ICON_COUNT+4;
    bool added=index>=LAUNCHER_ICON_COUNT;
    ui_obj_set_hidden(original,added); ui_obj_set_hidden(image,!added||is_mimi);
    ui_obj_set_hidden(mimi,!is_mimi);
    if (!added) launcher_icon_set(original,(launcher_icon_t)index);
    else if(!is_mimi) {
        // 新入口只在末尾追加，保留已发布的应用索引和图标身份。
        // Append new entries so shipped app indices retain their icon identity.
        const lv_image_dsc_t *const sources[]={&companion_sprout,&companion_disc_3,&companion_mic,&companion_watchfaces};
        _Static_assert(sizeof sources/sizeof sources[0]+1==COMPANION_ICON_COUNT,"Keep appended icon sources in sync");
        lv_image_set_src(image,sources[index-LAUNCHER_ICON_COUNT]);
    }
}
lv_obj_t *companion_launcher_icon_create(lv_obj_t *parent, int index) {
    lv_obj_t *container=lv_obj_create(parent); lv_obj_remove_style_all(container);
    lv_obj_set_size(container,LAUNCHER_ICON_SIZE,LAUNCHER_ICON_SIZE);
    ui_obj_set_scrollable(container,false); ui_obj_set_clickable(container,false); ui_obj_set_event_bubble(container,true);
    lv_obj_t *original=launcher_icon_create(container,LAUNCHER_WIFI); lv_obj_center(original);
    lv_obj_t *image=lv_image_create(container); lv_obj_center(image);
    lv_obj_t *mimi=lv_obj_create(container); lv_obj_remove_style_all(mimi);
    lv_obj_set_size(mimi,LAUNCHER_ICON_SIZE,LAUNCHER_ICON_SIZE);
    ui_obj_set_scrollable(mimi,false);ui_obj_set_clickable(mimi,false);ui_obj_set_event_bubble(mimi,true);
    lv_obj_add_event_cb(mimi,mimi_icon,LV_EVENT_DRAW_MAIN,NULL);
    lv_obj_set_style_image_recolor(image,lv_color_hex(0xf5f5f2),0);
    lv_obj_set_style_image_recolor_opa(image,LV_OPA_COVER,0);
    companion_launcher_icon_set(container,index); return container;
}
