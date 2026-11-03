#include "mimi_pet.h"
#include "mimi_motion.h"
#include <math.h>
#include <string.h>
#include "mimi_expressions.inc"

static float clamp(float value, float low, float high) {
    return fminf(high, fmaxf(low, value));
}
static uint32_t random_next(mimi_pet_t *pet) {
    uint32_t value = pet->rng;
    value ^= value << 13; value ^= value >> 17; value ^= value << 5;
    return pet->rng = value;
}
static void expression(mimi_pet_t *pet, uint8_t index) {
    pet->expression = index;
    pet->expression_seen |= 1u << index;
    pet->expression_ms = 0;
}
static void mood(mimi_pet_t *pet, mimi_mood_t next, uint8_t eye) {
    pet->mood = next;
    pet->mood_ms = 0;
    expression(pet, eye);
}
void mimi_pet_init(mimi_pet_t *pet) {
    memset(pet, 0, sizeof(*pet));
    pet->rng = 0x6d696d69;
    pet->lid = 1;
    pet->blink_due = 1800;
    pet->next_motion = 1;
    pet->expression_seen = 1;
    pet->motion_seen = 1;
    for (unsigned eye = 0; eye < 2; ++eye)
        for (unsigned i = 0; i < MIMI_EYE_POINTS; ++i)
            pet->eyes[eye][i] = (mimi_point_t){
                mimi_expressions[0][eye][i][0] * .01f,
                mimi_expressions[0][eye][i][1] * .01f};
}
void mimi_pet_touch(mimi_pet_t *pet, float x, float y, bool down) {
    pet->touching = down;
    if (!down) return;
    pet->target_x = isfinite(x) ? clamp(x, -1, 1) : 0;
    pet->target_y = isfinite(y) ? clamp(y, -1, 1) : 0;
    pet->quiet_ms = 0;
    if (pet->mood == MIMI_SLEEPING || pet->mood == MIMI_DROWSY)
        mood(pet, MIMI_SURPRISED, 3);
}
void mimi_pet_react(mimi_pet_t *pet, bool stroke) {
    pet->quiet_ms = 0;
    pet->touching = false;
    if (stroke) {
        mood(pet, MIMI_HAPPY, 2);
        mimi_pet_play(pet, pet->next_motion);
        pet->next_motion = pet->next_motion % (MIMI_MOTION_COUNT-1) + 1;
    } else {
        pet->expression_cursor = (pet->expression_cursor + 1) % MIMI_EXPRESSION_COUNT;
        mood(pet, MIMI_CURIOUS, pet->expression_cursor);
    }
    pet->blink_due = 1200 + random_next(pet) % 1800;
    pet->blink_ms = 0;
}
void mimi_pet_play(mimi_pet_t *pet, unsigned motion) {
    if (motion >= MIMI_MOTION_COUNT) return;
    // 连续请求只保留一个待播动作，避免中途把身体瞬移回圆形。
    // Coalesce interruptions into one queued clip instead of snapping the body to a circle.
    if (pet->motion_active) { pet->queued_motion=(uint8_t)motion; pet->motion_queued=true; return; }
    pet->motion = (uint8_t)motion;
    pet->motion_ms = pet->motion_wait_ms = 0;
    pet->motion_active = motion != MIMI_MOTION_IDLE;
    pet->motion_seen |= (uint16_t)(1u << motion);
}
float mimi_pet_motion_mix(const mimi_pet_t *pet) {
    if (!pet->motion_active) return 0;
    float enter=clamp(pet->motion_ms/450.f,0,1);
    float leave=clamp((mimi_motion_duration(pet->motion)+450.f-pet->motion_ms)/450.f,0,1);
    float blend=fminf(enter,leave);
    return blend*blend*(3-2*blend);
}
void mimi_pet_step(mimi_pet_t *pet, uint32_t elapsed_ms) {
    // 防止调度阻塞后追赶大量动画；遮挡时间由 App 层完全剔除。
    // Bound delayed frames; the app excludes all time spent behind an overlay.
    uint32_t dt = elapsed_ms > 80 ? 80 : elapsed_ms;
    if (!dt) return;
    pet->clock += dt; pet->mood_ms += dt;
    // 身体动作使用自己的眼睛轨迹；此时不能消耗普通表情的可见停留时间。
    // Body clips own their eye paths, so they must not consume a normal expression's visible hold time.
    if (!pet->motion_active) pet->expression_ms += dt;
    if (!pet->touching && pet->quiet_ms < 240000) pet->quiet_ms += dt;
    if (pet->motion_active) {
        pet->motion_ms += dt;
        if (pet->motion_ms >= mimi_motion_duration(pet->motion)+450) {
            pet->motion_active=false;
            if (pet->motion_queued) {
                pet->motion_queued=false;
                mimi_pet_play(pet,pet->queued_motion);
            }
        }
    } else if (!pet->touching && pet->mood != MIMI_SLEEPING && pet->mood != MIMI_DROWSY) {
        pet->motion_wait_ms += dt;
        if (pet->motion_wait_ms >= 5000) {
            mimi_pet_play(pet,pet->next_motion);
            pet->next_motion=pet->next_motion%(MIMI_MOTION_COUNT-1)+1;
        }
    }
    // 给完整动作库留出播放时间后才进入长时间待机睡眠。
    // Let the complete motion catalog play before long-idle sleep begins.
    if (pet->quiet_ms >= 210000 && pet->mood != MIMI_SLEEPING)
        mood(pet, MIMI_SLEEPING, 13);
    else if (pet->quiet_ms >= 180000 && pet->mood != MIMI_DROWSY && pet->mood != MIMI_SLEEPING)
        mood(pet, MIMI_DROWSY, 22);
    else if (pet->quiet_ms < 180000 && pet->mood != MIMI_IDLE && pet->mood_ms > 2800 && !pet->touching)
        mood(pet, MIMI_IDLE, pet->expression);
    if (pet->mood == MIMI_IDLE && !pet->motion_active && pet->expression_ms > 2600) {
        pet->expression_cursor=(pet->expression_cursor+1)%MIMI_EXPRESSION_COUNT;
        expression(pet,pet->expression_cursor);
        if (!pet->touching) {
            pet->target_x = ((int)(random_next(pet) % 201) - 100) * .006f;
            pet->target_y = ((int)(random_next(pet) % 201) - 100) * .004f;
        }
    }
    float blend = 1 - expf(-(float)dt / 130);
    for (unsigned eye = 0; eye < 2; ++eye)
        for (unsigned i = 0; i < MIMI_EYE_POINTS; ++i) {
            mimi_point_t *point = &pet->eyes[eye][i];
            point->x += (mimi_expressions[pet->expression][eye][i][0] * .01f - point->x) * blend;
            point->y += (mimi_expressions[pet->expression][eye][i][1] * .01f - point->y) * blend;
        }
    pet->gaze_x += (pet->target_x - pet->gaze_x) * blend;
    pet->gaze_y += (pet->target_y - pet->gaze_y) * blend;
    pet->lid = 1;
    if (pet->mood != MIMI_SLEEPING && pet->mood != MIMI_DROWSY) {
        pet->blink_ms += dt;
        if (pet->blink_ms >= pet->blink_due) {
            uint32_t blink = pet->blink_ms - pet->blink_due;
            pet->lid = blink < 75 ? 1 - blink / 75.f : clamp((blink - 75) / 135.f, .06f, 1);
            if (blink >= 210) {
                pet->blink_ms = 0;
                pet->blink_due = 2200 + random_next(pet) % 4200;
            }
        }
    }
    float phase = pet->mood_ms / 1000.f;
    pet->bounce = pet->mood == MIMI_HAPPY && phase < 1.8f ?
        -12 * fabsf(sinf(phase * 9)) * expf(-phase * 1.5f) : 0;
}
