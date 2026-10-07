#pragma once
#include "lvgl.h"

LV_FONT_DECLARE(font_identity_16);
LV_FONT_DECLARE(font_identity_26);
LV_FONT_DECLARE(font_identity_34);

#define IDENTITY_BOOT_MS 1800

// Native geometry matches artwork/identity/logo-dark.svg; no SVG or video decoder.
lv_obj_t *identity_logo_create(lv_obj_t *parent, int size);
// Call in the LVGL context after the original startup screen is ready.
// Covers this parent once, then deletes itself and its animation after 1.8s.
lv_obj_t *identity_boot_create(lv_obj_t *parent);
