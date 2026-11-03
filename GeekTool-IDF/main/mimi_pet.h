#pragma once
#include <stdbool.h>
#include <stdint.h>

#define MIMI_EYE_POINTS 48
#define MIMI_EXPRESSION_COUNT 25
typedef struct { float x, y; } mimi_point_t;
typedef enum {
    MIMI_IDLE, MIMI_CURIOUS, MIMI_HAPPY, MIMI_SURPRISED,
    MIMI_SHY, MIMI_DROWSY, MIMI_SLEEPING
} mimi_mood_t;

// 纯内存宠物状态；只累计可见时间，不联网、不写存储。
// An in-memory pet clock advances only while visible; no network or persistence.
typedef struct {
    mimi_point_t eyes[2][MIMI_EYE_POINTS];
    mimi_mood_t mood;
    uint32_t clock, quiet_ms, mood_ms, expression_ms, blink_ms, blink_due, rng;
    float gaze_x, gaze_y, target_x, target_y, lid, bounce;
    uint8_t expression, reaction, expression_cursor, motion, next_motion, queued_motion;
    uint32_t motion_ms, motion_wait_ms, expression_seen;
    uint16_t motion_seen;
    bool touching, motion_active, motion_queued;
} mimi_pet_t;

void mimi_pet_init(mimi_pet_t *pet);
void mimi_pet_step(mimi_pet_t *pet, uint32_t elapsed_ms);
void mimi_pet_touch(mimi_pet_t *pet, float x, float y, bool down);
void mimi_pet_react(mimi_pet_t *pet, bool stroke);
void mimi_pet_play(mimi_pet_t *pet, unsigned motion);
float mimi_pet_motion_mix(const mimi_pet_t *pet);
