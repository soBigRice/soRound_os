#include "pixel_field.h"
#include <math.h>
#include <string.h>

static float bounded(float value, float low, float high) {
    return fmaxf(low, fminf(high, value));
}

static void confine(pixel_field_tile_t *tile) {
    // Keep moving blocks clear of the shared header and the hint/buttons.
    tile->x = bounded(tile->x, 48, 418);
    tile->y = bounded(tile->y, 122, 329);
    float dx = tile->x - 233, dy = tile->y - 233;
    float distance = hypotf(dx, dy);
    if (distance > 190) {
        tile->x = 233 + dx * 190 / distance;
        tile->y = 233 + dy * 190 / distance;
        tile->vx *= .4f;
        tile->vy *= .4f;
    }
}

void pixel_field_init(pixel_field_t *field, unsigned pattern) {
    if (!field) return;
    memset(field, 0, sizeof(*field));
    pixel_field_repattern(field, pattern, false);
}

void pixel_field_repattern(pixel_field_t *field, unsigned pattern, bool scatter) {
    if (!field) return;
    static const int8_t stripes[] = {3, 2, 5, 1, 0};
    field->pattern = pattern % PIXEL_FIELD_PATTERNS;
    field->count = 0;
    field->touching = false;
    for (int y = 0; y < 15; ++y) for (int x = 0; x < 25; ++x) {
        int px = 65 + x * 14, py = 123 + y * 14;
        if ((px - 233) * (px - 233) + (py - 233) * (py - 233) > 190 * 190) continue;
        float crest = 4 + 2 * sinf(x * .31f + field->pattern * 1.1f) +
                      1.1f * cosf(x * .64f - field->pattern);
        bool lit = y >= crest && y < crest + 7;
        int stripe = (int)floorf((y - crest) / 1.8f);
        pixel_field_tile_t *tile = &field->tiles[field->count];
        *tile = (pixel_field_tile_t){.x = px, .y = py, .tx = px, .ty = py,
            .color = lit ? stripes[(int)bounded(stripe, 0, 4)] : -1};
        if (scatter) {
            float angle = field->count * 2.39996323f;
            float radius = 50 + field->count * 29 % 130;
            tile->x = 233 + cosf(angle) * radius;
            tile->y = 226 + sinf(angle) * radius;
            tile->vx = cosf(angle) * 4;
            tile->vy = sinf(angle) * 4;
            confine(tile);
        }
        ++field->count;
    }
    field->active = scatter;
}

void pixel_field_touch(pixel_field_t *field, float x, float y, bool touching) {
    if (!field) return;
    if (!touching) { field->touching = false; return; }
    if (!isfinite(x) || !isfinite(y) || x < 42 || x > 424 || y < 116 || y > 335) return;
    field->touch_x = x;
    field->touch_y = y;
    field->touching = true;
    field->active = true;
}

void pixel_field_gather(pixel_field_t *field) {
    if (!field) return;
    field->touching = false;
    for (unsigned i = 0; i < field->count; ++i) {
        field->tiles[i].vx = field->tiles[i].vy = 0;
    }
    field->active = true;
}

bool pixel_field_step(pixel_field_t *field, float seconds) {
    if (!field || !field->active || !isfinite(seconds) || seconds <= 0) return false;
    float dt = fminf(seconds, .04f) * 60;
    float drag = powf(.79f, dt);
    bool changed = false, moving = field->touching;
    for (unsigned i = 0; i < field->count; ++i) {
        pixel_field_tile_t *tile = &field->tiles[i];
        int old_x = (int)lroundf(tile->x), old_y = (int)lroundf(tile->y);
        float fx = (tile->tx - tile->x) * .048f;
        float fy = (tile->ty - tile->y) * .048f;
        if (field->touching) {
            float dx = tile->x - field->touch_x, dy = tile->y - field->touch_y;
            float distance = hypotf(dx, dy);
            if (distance < 65) {
                // A tap at the exact cell center must still dislodge it deterministically.
                if (distance < .01f) { dx = i % 2 ? 1 : -1; dy = 0; distance = 1; }
                float force = (65 - distance) * .09f;
                fx += dx * force / distance;
                fy += dy * force / distance;
            }
        }
        tile->vx = (tile->vx + fx * dt) * drag;
        tile->vy = (tile->vy + fy * dt) * drag;
        tile->x += tile->vx * dt;
        tile->y += tile->vy * dt;
        confine(tile);
        if (!field->touching && fabsf(tile->x - tile->tx) + fabsf(tile->y - tile->ty) < .12f &&
            fabsf(tile->vx) + fabsf(tile->vy) < .04f) {
            tile->x = tile->tx; tile->y = tile->ty; tile->vx = tile->vy = 0;
        } else moving = true;
        changed |= old_x != (int)lroundf(tile->x) || old_y != (int)lroundf(tile->y);
    }
    field->active = moving;
    return changed;
}

void pixel_shake_reset(pixel_shake_t *shake) {
    if (shake) memset(shake, 0, sizeof(*shake));
}

bool pixel_shake_sample(pixel_shake_t *shake, float x, float y, float z, uint32_t now_ms) {
    if (!shake) return false;
    if (!isfinite(x) || !isfinite(y) || !isfinite(z) ||
        fabsf(x) > 2.1f || fabsf(y) > 2.1f || fabsf(z) > 2.1f) {
        // Broken samples cannot accumulate peaks or count as quiet rearming time.
        shake->peak = shake->fell = shake->quiet = false;
        return false;
    }
    float magnitude = sqrtf(x * x + y * y + z * z);
    if (shake->blocked) {
        if (magnitude >= .85f && magnitude <= 1.2f) {
            if (!shake->quiet) { shake->quiet = true; shake->quiet_at = now_ms; }
            if ((uint32_t)(now_ms - shake->quiet_at) >= 300 &&
                (uint32_t)(now_ms - shake->fired_at) >= 900) {
                shake->blocked = false; shake->quiet = false;
            }
        } else shake->quiet = false;
        return false;
    }
    uint32_t elapsed = now_ms - shake->peak_at;
    if (shake->peak && elapsed > 250) shake->peak = shake->fell = false;
    if (magnitude > 1.55f) {
        if (shake->peak && shake->fell && elapsed >= 40) {
            shake->peak = shake->fell = false; shake->blocked = true; shake->quiet = false;
            shake->fired_at = now_ms;
            return true;
        }
        if (!shake->peak) { shake->peak = true; shake->peak_at = now_ms; }
    } else if (shake->peak && magnitude < 1.3f) {
        // Require an observed valley so one sustained impact cannot become two peaks.
        shake->fell = true;
    }
    return false;
}
