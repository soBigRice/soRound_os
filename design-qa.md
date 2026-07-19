# OTA Orbit Console Design QA

## Scope

- Target: `GeekTool-IDF/main/app_ota.c`
- Display: 466×466 round AMOLED
- Selected direction: Orbit Console (option 1)
- Reference: generated option selected by the user, checked together with a 466×466 geometry prototype

## Source-to-implementation checks

| Check | Result | Evidence |
|---|---|---|
| Single visual frame | Passed | OTA no longer creates a complete inner `lv_arc`; the global battery ring remains the only full circle. |
| Header hierarchy | Passed | Global back/title remain unchanged; current version sits in the orbit's top opening without crossing a line. |
| Primary action | Passed | Download glyph and “检查更新” share one 214×178 touch target in the visual center. |
| Orbit anatomy | Passed | 54 native LVGL dots form a 260° open orbit; idle, checking and real download progress have distinct rendering. |
| Secondary control | Passed | Beta channel is grouped in a compact bottom capsule and disabled while the OTA task is busy. |
| State coverage | Passed | Idle, checking, running, success, up-to-date and failure states were rendered in the geometry prototype. |
| Build compatibility | Passed | ESP-IDF 6.0.1 builds the changed LVGL code with warnings treated as errors. |

## Visible deviations from the concept

- The concept's thin green perimeter is rendered by the product's existing 8px global battery/charging arc; OTA does not own or restyle it.
- The local prototype uses library vector icons only to verify spacing. Firmware keeps the product's native dotted `glyph_line` download/check/cross icons.
- The bottom capsule is slightly wider than the concept so the existing 54×28 switch and Chinese label retain usable touch and text spacing.

## Interaction and hardware status

- The prototype verified the main CTA, beta switch and all visible states at the target 466×466 geometry without clipping or text/line collisions.
- Firmware compilation and static state-path checks pass. AMOLED brightness, actual touch hit testing and animation smoothness still require a device flash and photo/video; these are not claimed as hardware-verified here.

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
