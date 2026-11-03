#pragma once
#include "mimi_pet.h"

#define MIMI_BODY_POINTS 64
#define MIMI_MOTION_COUNT 14
typedef enum {
    MIMI_MOTION_IDLE, MIMI_MOTION_THINKING, MIMI_MOTION_WINK, MIMI_MOTION_WIDE,
    MIMI_MOTION_ALERT, MIMI_MOTION_NOTIFY, MIMI_MOTION_EXCLAIM, MIMI_MOTION_SLEEP,
    MIMI_MOTION_EGG, MIMI_MOTION_HEXAGON, MIMI_MOTION_PLAY, MIMI_MOTION_ORBIT,
    MIMI_MOTION_BURST, MIMI_MOTION_COMET
} mimi_motion_id_t;
typedef struct { float a,k,tilt,speed,phase,sweep,width,cx,cy,hue,hue_span; } mimi_arc_seed_t;
typedef struct {
    mimi_point_t body[MIMI_BODY_POINTS], eyes[2][MIMI_EYE_POINTS];
    float dots[5][3], notch[4], dot_opa[5], eye_opa[2], arc_opa[6], body_opa;
    uint8_t arc_kind;
    bool dots_behind;
} mimi_motion_pose_t;

// 本地固定点矢量关键帧，设备上按可见时钟连续插值。
// Locally baked vector keyframes interpolate against the visible device clock.
void mimi_motion_sample(unsigned id, uint32_t elapsed_ms, mimi_motion_pose_t *pose);
uint32_t mimi_motion_duration(unsigned id);
const char *mimi_motion_name(unsigned id);
const mimi_arc_seed_t *mimi_motion_arcs(unsigned kind, unsigned *count);
