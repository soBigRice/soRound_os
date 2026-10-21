#include "garden_state.h"
#include "nvs.h"
#include "ui_persistence.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

#define GARDEN_MAGIC UINT32_C(0x47524431)
#define MAX_PROGRESS UINT32_C(999999999)
typedef struct {
    uint32_t magic, focus_minutes, focus_sessions, waterings;
    uint32_t day_key, today_minutes, last_focus_id, light;
} garden_record_t;
_Static_assert(sizeof(garden_record_t) == 32, "garden NVS v1 format must remain 32 bytes");
static garden_state_t s_shared;
static bool s_ready, s_dirty;

static bool valid(const garden_state_t *s) {
    return s && s->focus_minutes <= MAX_PROGRESS && s->focus_sessions <= MAX_PROGRESS &&
        s->waterings <= MAX_PROGRESS && s->today_minutes <= s->focus_minutes &&
        s->light >= GARDEN_LIGHT_AUTO && s->light <= GARDEN_LIGHT_NIGHT;
}
bool garden_state_load(garden_state_t *out) {
    if (!out) return false;
    garden_record_t r = {0}; size_t bytes = sizeof r;
    nvs_handle_t h;
    esp_err_t err = nvs_open("garden", NVS_READONLY, &h);
    if (err == ESP_OK) {
        err = nvs_get_blob(h, "state_v1", &r, &bytes); nvs_close(h);
    }
    if (err == ESP_ERR_NVS_NOT_FOUND) { *out = (garden_state_t){0}; return true; }
    if (err != ESP_OK || bytes != sizeof r || r.magic != GARDEN_MAGIC || r.light > GARDEN_LIGHT_NIGHT) return false;
    garden_state_t s = {r.focus_minutes, r.focus_sessions, r.waterings, r.day_key,
                        r.today_minutes, r.last_focus_id, (garden_light_t)r.light};
    if (!valid(&s)) return false;
    *out = s; return true;
}
bool garden_state_save(const garden_state_t *s) {
    if (!valid(s)) return false;
    garden_record_t r = {GARDEN_MAGIC, s->focus_minutes, s->focus_sessions, s->waterings,
                         s->day_key, s->today_minutes, s->last_focus_id, (uint32_t)s->light};
    nvs_handle_t h; esp_err_t err = nvs_open("garden", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, "state_v1", &r, sizeof r);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    return err == ESP_OK;
}
void garden_state_roll_day(garden_state_t *s, uint32_t key) {
    // 未校时不能反复清空“今日”；获得可信日期后才切日。
    // An unset clock must not reset today's value; roll only with a trusted date.
    if (s && key && s->day_key != key) { s->day_key = key; s->today_minutes = 0; }
}
bool garden_state_water(garden_state_t *s) {
    if (!valid(s) || s->waterings == MAX_PROGRESS) return false;
    ++s->waterings; return true;
}
bool garden_state_complete_focus(garden_state_t *s, uint32_t id, uint32_t minutes, uint32_t key) {
    if (!valid(s) || !id || id <= s->last_focus_id || !minutes || minutes > 1440 ||
        s->focus_minutes > MAX_PROGRESS - minutes || s->focus_sessions == MAX_PROGRESS) return false;
    garden_state_roll_day(s, key);
    s->focus_minutes += minutes; s->today_minutes += minutes; ++s->focus_sessions; s->last_focus_id = id;
    return true;
}
uint32_t garden_state_next_focus_id(const garden_state_t *s) {
    return s && s->last_focus_id < UINT32_MAX ? s->last_focus_id + 1 : 0;
}
uint32_t garden_state_leaves(const garden_state_t *s) { return s ? s->focus_sessions : 0; }
uint32_t garden_day_key(void) {
    time_t now = time(NULL); struct tm local;
    if (!localtime_r(&now, &local) || local.tm_year < 124) return 0;
    return (uint32_t)(local.tm_year + 1900) * 10000 + (uint32_t)(local.tm_mon + 1) * 100 + (uint32_t)local.tm_mday;
}
bool garden_is_night(garden_light_t light) {
    if (light != GARDEN_LIGHT_AUTO) return light == GARDEN_LIGHT_NIGHT;
    time_t now = time(NULL); struct tm local;
    if (!localtime_r(&now, &local) || local.tm_year < 124) return false;
    return local.tm_hour < 7 || local.tm_hour >= 19;
}
garden_state_t *garden_shared_state(void) {
    if (!s_ready) s_ready = garden_state_load(&s_shared);
    return &s_shared;
}
bool garden_shared_ready(void) { return s_ready; }
bool garden_shared_dirty(void) {
    if(s_dirty && ui_persistence_saved(UI_STORE_GARDEN,&s_shared,sizeof s_shared))s_dirty=false;
    return s_dirty;
}
void garden_shared_mark_dirty(void) { if (s_ready) s_dirty = true; }
static bool write_shared_garden(const void *snapshot){return garden_state_save(snapshot);}
bool garden_shared_save(void) {
    if (!s_ready) return false;
    if (s_dirty && ui_persistence_save(UI_STORE_GARDEN,&s_shared,sizeof s_shared,write_shared_garden)) s_dirty = false;
    return !s_dirty;
}
