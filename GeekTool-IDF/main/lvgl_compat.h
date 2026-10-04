#pragma once
#include "lvgl.h"

// 本地 LVGL 9.5 与 CI 9.6 都受支持。9.6 推荐独立 flag setter,旧版本保持同样的位操作。
#if LV_VERSION_CHECK(9, 6, 0)
#define ui_obj_set_scrollable lv_obj_set_scrollable
#define ui_obj_set_clickable lv_obj_set_clickable
#define ui_obj_set_event_bubble lv_obj_set_event_bubble
#define ui_obj_set_gesture_bubble lv_obj_set_gesture_bubble
#define ui_obj_set_hidden lv_obj_set_hidden
#define ui_obj_is_hidden lv_obj_is_hidden
#define ui_obj_set_scroll_elastic lv_obj_set_scroll_elastic
#define ui_obj_set_scroll_momentum lv_obj_set_scroll_momentum
#define ui_obj_set_scroll_chain_hor lv_obj_set_scroll_chain_hor
#define ui_obj_set_scroll_chain_ver lv_obj_set_scroll_chain_ver
#define ui_obj_set_send_draw_task_events lv_obj_set_send_draw_task_events
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
static inline void ui_obj_set_hidden(lv_obj_t *obj, bool hidden) {
    lv_obj_set_flag(obj, LV_OBJ_FLAG_HIDDEN, hidden);
}
static inline bool ui_obj_is_hidden(const lv_obj_t *obj) {
    return lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static inline void ui_obj_set_scroll_elastic(lv_obj_t *obj, bool enabled) {
    lv_obj_set_flag(obj, LV_OBJ_FLAG_SCROLL_ELASTIC, enabled);
}
static inline void ui_obj_set_scroll_momentum(lv_obj_t *obj, bool enabled) {
    lv_obj_set_flag(obj, LV_OBJ_FLAG_SCROLL_MOMENTUM, enabled);
}
static inline void ui_obj_set_scroll_chain_hor(lv_obj_t *obj, bool enabled) {
    lv_obj_set_flag(obj, LV_OBJ_FLAG_SCROLL_CHAIN_HOR, enabled);
}
static inline void ui_obj_set_scroll_chain_ver(lv_obj_t *obj, bool enabled) {
    lv_obj_set_flag(obj, LV_OBJ_FLAG_SCROLL_CHAIN_VER, enabled);
}
static inline void ui_obj_set_send_draw_task_events(lv_obj_t *obj, bool enabled) {
    lv_obj_set_flag(obj, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS, enabled);
}
#endif
