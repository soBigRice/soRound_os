#pragma once
#include <stdbool.h>
#include <stdint.h>
#define FOCUS_DURATION_US INT64_C(1500000000)
typedef enum { FOCUS_READY, FOCUS_RUNNING, FOCUS_PAUSED, FOCUS_DONE } focus_phase_t;
typedef struct {
    focus_phase_t phase;
    int64_t elapsed_us, started_us;
    uint32_t session_id;
    bool completion_pending;
} focus_state_t;
typedef struct { uint8_t scene; bool muted; } focus_preferences_t;

void focus_state_init(focus_state_t *s, uint32_t session_id);
void focus_state_toggle(focus_state_t *s, int64_t now_us);
void focus_state_reset(focus_state_t *s, uint32_t session_id);
void focus_state_update(focus_state_t *s, int64_t now_us);
int64_t focus_state_remaining_us(const focus_state_t *s, int64_t now_us);
bool focus_state_take_completion(focus_state_t *s);
bool focus_preferences_load(focus_preferences_t *out);
bool focus_preferences_save(const focus_preferences_t *preferences);
