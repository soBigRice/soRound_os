#include "focus_state.h"
#include "nvs.h"
#include <stddef.h>

static int64_t elapsed(const focus_state_t *s, int64_t now) {
    int64_t extra = s->phase == FOCUS_RUNNING && now > s->started_us ? now - s->started_us : 0;
    return extra >= FOCUS_DURATION_US - s->elapsed_us ? FOCUS_DURATION_US : s->elapsed_us + extra;
}
void focus_state_init(focus_state_t *s, uint32_t id) { if (s) *s = (focus_state_t){.session_id = id}; }
void focus_state_update(focus_state_t *s, int64_t now) {
    if (s && s->phase == FOCUS_RUNNING && elapsed(s, now) >= FOCUS_DURATION_US) {
        s->elapsed_us = FOCUS_DURATION_US; s->phase = FOCUS_DONE; s->completion_pending = true;
    }
}
void focus_state_toggle(focus_state_t *s, int64_t now) {
    if (!s || !s->session_id) return;
    focus_state_update(s, now);
    if (s->phase == FOCUS_RUNNING) { s->elapsed_us = elapsed(s, now); s->phase = FOCUS_PAUSED; }
    else if (s->phase != FOCUS_DONE) { s->started_us = now; s->phase = FOCUS_RUNNING; }
}
void focus_state_reset(focus_state_t *s, uint32_t id) { focus_state_init(s, id); }
int64_t focus_state_remaining_us(const focus_state_t *s, int64_t now) {
    return s ? FOCUS_DURATION_US - elapsed(s, now) : FOCUS_DURATION_US;
}
bool focus_state_take_completion(focus_state_t *s) {
    if (!s || !s->completion_pending) return false;
    s->completion_pending = false; return true;
}
bool focus_preferences_load(focus_preferences_t *out) {
    if (!out) return false;
    uint32_t value = 0; nvs_handle_t h; esp_err_t err = nvs_open("focus", NVS_READONLY, &h);
    if (err == ESP_OK) { err = nvs_get_u32(h, "prefs_v1", &value); nvs_close(h); }
    if (err == ESP_ERR_NVS_NOT_FOUND) { *out = (focus_preferences_t){.scene = 1}; return true; }
    if (err != ESP_OK || (value & ~UINT32_C(0x103)) || !(value & 3)) return false;
    *out = (focus_preferences_t){.scene = value & 3, .muted = !!(value & 0x100)}; return true;
}
bool focus_preferences_save(const focus_preferences_t *p) {
    if (!p || p->scene < 1 || p->scene > 3) return false;
    uint32_t value = p->scene | (p->muted ? 0x100 : 0); nvs_handle_t h;
    esp_err_t err = nvs_open("focus", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u32(h, "prefs_v1", value);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    return err == ESP_OK;
}
