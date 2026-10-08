#pragma once
#include "lvgl.h"

LV_FONT_DECLARE(font_identity_16);
LV_FONT_DECLARE(font_identity_18);
LV_FONT_DECLARE(font_identity_26);
LV_FONT_DECLARE(font_identity_34);

#define IDENTITY_BOOT_MS 4000

// Native geometry matches artwork/identity/logo-dark.svg; no SVG or video decoder.
lv_obj_t *identity_logo_create(lv_obj_t *parent, int size);
// All boot APIs run in the LVGL context; create may precede interactive pages.
// Holds until both the minimum duration and explicit startup release are met.
lv_obj_t *identity_boot_create(lv_obj_t *parent);
void identity_boot_message(lv_obj_t *boot, const char *message, bool error);
// Reveal home for the final display check, retaining the input/power guard
// and an intact error cover until the pending OTA is confirmed.
void identity_boot_reveal(lv_obj_t *boot);
void identity_boot_release(lv_obj_t *boot);
bool identity_boot_active(void);
