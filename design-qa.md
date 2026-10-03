# OTA Minimal Update Page Design QA

Checked: 2026-10-03. The user approved the native OTA rendering with the full-screen dotted ring and filled dotted arrow, and authorized a combined weather/OTA release. Device acceptance remains pending.

- Target: `GeekTool-IDF/main/app_ota.c`, 466×466 round AMOLED.
- Direction: 438px dotted circle inside the 466px screen, large filled dotted arrow, thin download bar, and a small dotted settings button at the top right.
- Rendering: actual LVGL host renders in English and Chinese were inspected for idle, downloading, retrying, verifying, final/error states and settings. The host fixture recreates launcher chrome; these are not device screenshots.
- Interaction: pointer input exercises settings entry, Beta switch persistence and disabled state during download. Back consumes the settings level first; leaving/reentering the app restores the real background progress.
- Animation: the arrow moves upward and fades before resetting. The ring sweeps during checking and fills clockwise from actual download progress, with only the completed endpoint pulsing. Settings, occlusion and exit remove both animations.
- States: gray/white idle and checking, red download progress, amber retry, blue verification, green success/check, and red failure/cross.
- Round-screen check: all 26 bilingual renders and 66 motion frames fit the physical circular boundary. Long verification text was moved above the progress bar and split over two lines to preserve the power warning.
- Validation: firmware build, all six host regression groups on LVGL 9.5 and upstream 9.6.0, 18 OTA recovery/error groups and whitespace checks pass. No OTA assets or dependencies added. Released with the weather redesign as v1.7-beta.11; hardware appearance, touch and animation smoothness still require device/user acceptance.

The detailed state and lifecycle explanation is maintained in [OTA implementation notes](./GeekTool-IDF/PORTING_NOTES.md#2026-10-03-ota-极简页面).

## Settings Three-category Hub Design QA

### Scope

- Target: `GeekTool-IDF/main/app_settings.c`
- Display: 466×466 round AMOLED
- Selected direction: option 3, three-category capsule hub
- Reference: the exact generated option selected by the user, compared side by side with the 466×466 implementation prototype

### Source-to-implementation checks

| Check | Result | Evidence |
|---|---|---|
| First-screen hierarchy | Passed | The first screen contains only Display, Sound and System; seven concrete settings no longer compete on one long list. |
| Selected composition | Passed | Display/System use restrained black capsules; Sound uses the wider gray focus surface and red leading dot. |
| Round-screen safe area | Passed | 318/350px rows at y=110, 190 and 280 stay inside the global 8px ring and below the shared header. |
| Native visual language | Passed | Firmware icons use existing dotted glyph primitives and existing black/white/gray/red tokens; no new bitmap assets are introduced. |
| Information refresh | Passed | Hub and group builders re-read brightness, face, AOD, volume, silent and language values when navigating back. |
| Navigation | Passed | Hub → Display → Brightness → Display → Hub was exercised in the prototype; firmware implements the same level/group state machine. |
| Motion budget | Passed | Rows use 12px, 180–190ms ease-out entries with 40ms stagger; detail uses one 180ms entry and the app has no tick or looping animation. |
| Runtime overhead | Passed | Audio initialization is deferred until Volume is opened; non-audio settings no longer initialize codec/I2S on entry. |
| Build compatibility | Passed | ESP-IDF 6.0.1 produced a 0x1cfef0-byte image with 40% of the smallest OTA partition free. |

### Visible deviations from the concept

- The generated concept uses a thin perimeter stroke. Firmware retains the product-wide 8px launcher battery ring because it is a shared device-status layer.
- The geometry prototype uses Lucide icons with a dotted stroke to approximate density. Firmware uses the product's native `glyph_circle`, `glyph_line` and `glyph_arc` objects.
- The implementation title/back positions follow the existing launcher contract, which sits slightly higher than the free-standing generated concept.

### Interaction and hardware status

- The prototype production build passes, its browser console contains no warnings/errors, and category/detail/back navigation works at the target viewport.
- Reference and implementation were judged together in one local side-by-side artifact at the same 466×466 size; no clipping, edge collision or overlapping text is visible.
- Firmware compilation and static lifecycle review pass. AMOLED appearance, physical touch targeting, slider drag feel and animation frame rate remain device-OTA checks and are not claimed as hardware-verified.

final result: passed
