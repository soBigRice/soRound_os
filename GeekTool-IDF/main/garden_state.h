#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { GARDEN_LIGHT_AUTO, GARDEN_LIGHT_DAY, GARDEN_LIGHT_NIGHT } garden_light_t;
typedef struct {
    uint32_t focus_minutes, focus_sessions, waterings;
    uint32_t day_key, today_minutes, last_focus_id;
    garden_light_t light;
} garden_state_t;

// 数据只在读取成功后替换；失败不覆盖已在内存中的待保存进度。
// Replace state only after a valid read; errors preserve pending in-memory progress.
bool garden_state_load(garden_state_t *state);
bool garden_state_save(const garden_state_t *state);
void garden_state_roll_day(garden_state_t *state, uint32_t day_key);
bool garden_state_water(garden_state_t *state);
bool garden_state_complete_focus(garden_state_t *state, uint32_t session_id,
                                 uint32_t minutes, uint32_t day_key);
uint32_t garden_state_next_focus_id(const garden_state_t *state);
uint32_t garden_state_leaves(const garden_state_t *state);
uint32_t garden_day_key(void);
bool garden_is_night(garden_light_t light);

// 两个 App 共享唯一工作副本；只有成功 commit 后清除 dirty。
// Both apps share one working copy; only a successful commit clears dirty.
garden_state_t *garden_shared_state(void);
bool garden_shared_ready(void);
bool garden_shared_dirty(void);
void garden_shared_mark_dirty(void);
bool garden_shared_save(void);
