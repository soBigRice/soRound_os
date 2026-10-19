#include "garden_state.h"
#include "focus_state.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned opened, closed;
static int open_error, get_error, set_error, commit_error;
static uint8_t committed_blob[32], staged_blob[32];
static size_t blob_length;
static uint32_t committed_pref = 1, staged_pref;
static bool have_blob;
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *h) {
    (void)mode; assert(!strcmp(name, "garden") || !strcmp(name, "focus"));
    if (open_error) return open_error;
    *h = !strcmp(name, "garden") ? 1 : 2; ++opened; return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *value, size_t *length) {
    assert(h == 1 && !strcmp(key, "state_v1")); if (get_error) return get_error;
    if (!have_blob) return ESP_ERR_NVS_NOT_FOUND;
    assert(*length >= blob_length); memcpy(value, committed_blob, blob_length); *length = blob_length; return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *value, size_t length) {
    assert(h == 1 && !strcmp(key, "state_v1") && length == 32); if (set_error) return set_error;
    memcpy(staged_blob, value, length); return ESP_OK;
}
esp_err_t nvs_get_u32(nvs_handle_t h, const char *key, uint32_t *value) {
    assert(h == 2 && !strcmp(key, "prefs_v1")); if (get_error) return get_error; *value = committed_pref; return ESP_OK;
}
esp_err_t nvs_set_u32(nvs_handle_t h, const char *key, uint32_t value) {
    assert(h == 2 && !strcmp(key, "prefs_v1")); if (set_error) return set_error; staged_pref = value; return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {
    if (commit_error) return commit_error;
    if (h == 1) { memcpy(committed_blob, staged_blob, sizeof committed_blob); blob_length = sizeof committed_blob; have_blob = true; }
    else committed_pref = staged_pref;
    return ESP_OK;
}
void nvs_close(nvs_handle_t h) { assert(h == 1 || h == 2); ++closed; }

int main(void) {
    garden_state_t garden = {.focus_minutes = 99};
    assert(garden_state_load(&garden) && garden.focus_minutes == 0);
    open_error = ESP_FAIL; garden.focus_minutes = 99;
    assert(!garden_state_load(&garden) && garden.focus_minutes == 99); open_error = 0;
    garden = (garden_state_t){0};
    assert(garden_state_water(&garden) && garden.waterings == 1);
    assert(garden_state_complete_focus(&garden, 1, 25, 20261019));
    assert(!garden_state_complete_focus(&garden, 1, 25, 20261019));
    assert(!garden_state_complete_focus(&garden, 0, 25, 20261019));
    assert(garden.focus_minutes == 25 && garden.focus_sessions == 1 && garden.today_minutes == 25);
    assert(garden_state_next_focus_id(&garden) == 2 && garden_state_leaves(&garden) == 1);
    garden_state_roll_day(&garden, 0); assert(garden.today_minutes == 25);
    assert(garden_state_complete_focus(&garden, 2, 25, 20261020));
    assert(garden.focus_minutes == 50 && garden.today_minutes == 25 && garden.focus_sessions == 2);
    assert(garden_state_save(&garden));
    garden_state_t loaded = {0}; assert(garden_state_load(&loaded)); assert(!memcmp(&garden, &loaded, sizeof garden));
    assert(!garden_state_complete_focus(&loaded, 2, 25, 20261020));
    get_error = ESP_FAIL; loaded.focus_minutes = 19; assert(!garden_state_load(&loaded) && loaded.focus_minutes == 19); get_error = 0;
    committed_blob[0] ^= 1; assert(!garden_state_load(&loaded)); committed_blob[0] ^= 1;
    --blob_length; assert(!garden_state_load(&loaded)); ++blob_length;
    set_error = ESP_FAIL; assert(!garden_state_save(&garden)); set_error = 0;
    commit_error = ESP_FAIL; assert(!garden_state_save(&garden)); commit_error = 0;
    garden_state_t *shared = garden_shared_state(); assert(garden_shared_ready());
    assert(garden_state_water(shared)); garden_shared_mark_dirty();
    commit_error = ESP_FAIL; assert(!garden_shared_save() && garden_shared_dirty());
    commit_error = 0; assert(garden_shared_save() && !garden_shared_dirty());
    assert(garden_state_load(&loaded) && loaded.waterings == 2);

    focus_state_t focus; focus_state_init(&focus, 3);
    assert(focus.phase == FOCUS_READY && focus_state_remaining_us(&focus, 900) == FOCUS_DURATION_US);
    focus_state_toggle(&focus, 1000); assert(focus.phase == FOCUS_RUNNING);
    assert(focus_state_remaining_us(&focus, 1000 + 1000000) == FOCUS_DURATION_US - 1000000);
    assert(focus_state_remaining_us(&focus, 0) == FOCUS_DURATION_US); // backwards input cannot add time
    focus_state_toggle(&focus, 1000 + 1000000); assert(focus.phase == FOCUS_PAUSED);
    assert(focus_state_remaining_us(&focus, INT64_C(90000000000)) == FOCUS_DURATION_US - 1000000);
    focus_state_toggle(&focus, INT64_C(90000000000));
    int64_t deadline = INT64_C(90000000000) + FOCUS_DURATION_US - 1000000;
    focus_state_update(&focus, deadline - 1); assert(focus.phase == FOCUS_RUNNING && !focus_state_take_completion(&focus));
    focus_state_update(&focus, deadline); assert(focus.phase == FOCUS_DONE && focus_state_take_completion(&focus));
    focus_state_update(&focus, deadline + FOCUS_DURATION_US); assert(!focus_state_take_completion(&focus));
    focus_state_toggle(&focus, deadline + 1); assert(focus.phase == FOCUS_DONE);
    focus_state_reset(&focus, 4); assert(focus.phase == FOCUS_READY && focus.session_id == 4);
    focus_state_toggle(&focus, 0); focus_state_update(&focus, FOCUS_DURATION_US * 100);
    assert(focus.phase == FOCUS_DONE && focus_state_take_completion(&focus));
    focus_state_reset(&focus, 0); focus_state_toggle(&focus, 1); assert(focus.phase == FOCUS_READY);

    focus_preferences_t pref = {0}; assert(focus_preferences_load(&pref) && pref.scene == 1 && !pref.muted);
    pref.scene = 3; pref.muted = true; assert(focus_preferences_save(&pref));
    pref = (focus_preferences_t){0}; assert(focus_preferences_load(&pref) && pref.scene == 3 && pref.muted);
    committed_pref = 0; assert(!focus_preferences_load(&pref) && pref.scene == 3);
    committed_pref = 0x200; assert(!focus_preferences_load(&pref));
    commit_error = ESP_FAIL; assert(!focus_preferences_save(&pref)); commit_error = 0;
    assert(opened == closed);
    puts("garden/focus: monotonic deadlines, pause/resume, one-time growth, date rollover, isolated NVS corruption/error/commit and cleanup passed");
}
