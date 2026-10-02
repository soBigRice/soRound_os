#pragma once
#include "lvgl.h"

// 本地 LVGL 9.5 与 CI 9.6 都受支持。9.6 推荐独立 flag setter,旧版本保持同样的位操作。
#if LV_VERSION_CHECK(9, 6, 0)
#define ui_obj_set_scrollable lv_obj_set_scrollable
#define ui_obj_set_clickable lv_obj_set_clickable
#define ui_obj_set_event_bubble lv_obj_set_event_bubble
#define ui_obj_set_gesture_bubble lv_obj_set_gesture_bubble
#else
static inline void ui_obj_set_scrollable(lv_obj_t *obj, bool enabled) {
    lv_obj_set_flag(obj, LV_OBJ_FLAG_SCROLLABLE, enabled);
}
static inline void ui_obj_set_clickable(lv_obj_t *obj, bool enabled) {
    lv_obj_set_flag(obj, LV_OBJ_FLAG_CLICKABLE, enabled);
}
static inline void ui_obj_set_event_bubble(lv_obj_t *obj, bool enabled) {
    lv_obj_set_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE, enabled);
}
static inline void ui_obj_set_gesture_bubble(lv_obj_t *obj, bool enabled) {
    lv_obj_set_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE, enabled);
}
#endif
