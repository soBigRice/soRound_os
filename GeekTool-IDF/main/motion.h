#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

// 8.25ms 固定物理步长保留原 33ms×4 的速度;渲染频率不改变重力/阻尼。
#define FLUID_STEP_US 8250
static inline unsigned motion_steps(uint32_t *remainder, uint32_t elapsed_us) {
    if (elapsed_us > 99000) elapsed_us = 99000; // 限制卡顿补步,避免长时间阻塞 UI
    *remainder += elapsed_us;
    unsigned steps = *remainder / FLUID_STEP_US;
    *remainder %= FLUID_STEP_US;
    return steps;
}
static inline bool motion_wake(float tx, float ty, float anchor_x, float anchor_y) {
    // 相对入睡姿态比较;缓慢、累计的倾斜也必须唤醒。
    return fabsf(tx - anchor_x) + fabsf(ty - anchor_y) > 0.05f;
}
