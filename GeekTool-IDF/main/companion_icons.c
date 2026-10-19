#include "companion_icons.h"
#include "companion_icons.inc"

void companion_launcher_icon_set(lv_obj_t *icon, int index) {
    if (!icon || index < 0 || index >= LAUNCHER_ICON_COUNT + COMPANION_ICON_COUNT) return;
    lv_obj_t *original=lv_obj_get_child(icon,0), *image=lv_obj_get_child(icon,1);
    bool added=index>=LAUNCHER_ICON_COUNT;
    ui_obj_set_hidden(original,added); ui_obj_set_hidden(image,!added);
    if (!added) launcher_icon_set(original,(launcher_icon_t)index);
    else {
        const lv_image_dsc_t *const sources[]={&companion_sprout,&companion_disc_3,&companion_mic};
        lv_image_set_src(image,sources[index-LAUNCHER_ICON_COUNT]);
    }
}
lv_obj_t *companion_launcher_icon_create(lv_obj_t *parent, int index) {
    lv_obj_t *container=lv_obj_create(parent); lv_obj_remove_style_all(container);
    lv_obj_set_size(container,LAUNCHER_ICON_SIZE,LAUNCHER_ICON_SIZE);
    ui_obj_set_scrollable(container,false); ui_obj_set_clickable(container,false); ui_obj_set_event_bubble(container,true);
    lv_obj_t *original=launcher_icon_create(container,LAUNCHER_WIFI); lv_obj_center(original);
    lv_obj_t *image=lv_image_create(container); lv_obj_center(image);
    lv_obj_set_style_image_recolor(image,lv_color_hex(0xf5f5f2),0);
    lv_obj_set_style_image_recolor_opa(image,LV_OPA_COVER,0);
    companion_launcher_icon_set(container,index); return container;
}
