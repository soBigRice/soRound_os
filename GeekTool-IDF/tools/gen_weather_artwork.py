#!/usr/bin/env python3
"""Preserve the approved raster dot patterns as native LVGL 9 RGB565/RLE images.

Requires Pillow. Source crops exclude captions; main-screen coordinates are
normalized by the 528px reference display, not by the individual icon's bounds.
Library icons share the same 466/528 scale and are centered in the icon area.
"""
from pathlib import Path
import hashlib
import json
import struct
from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / "artwork/weather"
DAY = [0, 1, 2, 3, 45, 48, 51, 53, 55, 56, 57, 61, 63, 65, 66, 67]
OTHER = [71, 73, 75, 77, 80, 81, 82, 85, 86, 95, 97, 96, 99, "0n", "1n", "2n"]
MAIN = {
    3: ((150, 320, 440, 452), (31, 194)),
    0: ((750, 298, 932, 466), (577, 194)),
    61: ((1275, 312, 1503, 462), (1129, 194)),
}


def rgb565(image):
    out = bytearray()
    for r, g, b in list(image.convert("RGB").get_flattened_data()):
        out.extend(struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)))
    return bytes(out)


def native_rgb(data, size):
    # Matches the host framebuffer's RGB565 expansion, with no palette reduction.
    values = struct.iter_unpack("<H", data)
    pixels = [(((v >> 11) & 31) * 255 // 31,
               ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31) for (v,) in values]
    image = Image.new("RGB", size)
    image.putdata(pixels)
    return image


def rle(data):
    """LVGL's documented pixel RLE: literal high bit, repeat low bit, 127 cap."""
    words = [data[i:i + 2] for i in range(0, len(data), 2)]
    encoded = bytearray()
    i = 0
    while i < len(words):
        n = 1
        while i + n < len(words) and n < 127 and words[i + n] == words[i]:
            n += 1
        if n >= 3:
            encoded.append(n)
            encoded.extend(words[i])
            i += n
        else:
            start = i
            i += n
            while i < len(words) and i - start < 127:
                if i + 2 < len(words) and words[i] == words[i + 1] == words[i + 2]:
                    break
                i += 1
            encoded.append(0x80 | (i - start))
            encoded.extend(b"".join(words[start:i]))
    return struct.pack("<III", 1, len(encoded), len(data)) + encoded


def crop_pattern(image, box):
    cropped = image.crop(box).convert("RGB")
    # The generated presentation has low-luminance background grain. A bright
    # foreground seed plus a 2px halo keeps the original dot-edge pixels while
    # rejecting isolated grain; this mask never moves or redraws a dot.
    pixels = list(cropped.get_flattened_data())
    seed = Image.new("L", cropped.size)
    seed.putdata([255 if max(p) > 75 else 0 for p in pixels])
    halo = list(seed.filter(ImageFilter.MaxFilter(5)).get_flattened_data())
    pixels = [p if keep and max(p) > 24 else (0, 0, 0) for p, keep in zip(pixels, halo)]
    cropped.putdata(pixels)
    bounds = cropped.getbbox()
    assert bounds, box
    return cropped.crop(bounds), (box[0] + bounds[0], box[1] + bounds[1])


def build():
    patterns = {}
    metadata = {"display": [466, 466], "icon_area": [93, 92, 280, 160], "sources": {}, "icons": []}
    for filename, codes in [("icons-day.png", DAY), ("icons-other.png", OTHER)]:
        source = ART / "reference" / filename
        image = Image.open(source).convert("RGB")
        metadata["sources"][filename] = hashlib.sha256(source.read_bytes()).hexdigest()
        for i, code in enumerate(codes):
            col, row = i % 4, i // 4
            box = (40 + col * 305, [110, 380, 650, 925][row],
                   min(1214, 323 + col * 305), [288, 562, 832, 1095][row])
            cropped, origin = crop_pattern(image, box)
            scale = 466 / 528
            cropped = cropped.resize((round(cropped.width * scale), round(cropped.height * scale)), Image.Resampling.LANCZOS)
            assert cropped.width <= 280 and cropped.height <= 160, (code, cropped.size)
            patterns[str(code)] = (cropped, ((280 - cropped.width) // 2, (160 - cropped.height) // 2), filename, box)

    source = ART / "reference/screens.png"
    metadata["sources"]["screens.png"] = hashlib.sha256(source.read_bytes()).hexdigest()
    image = Image.open(source).convert("RGB")
    scale = 466 / 528
    for code, (box, screen_origin) in MAIN.items():
        cropped, origin = crop_pattern(image, box)
        # One proportional resize follows the whole screen's scale. Tight crop
        # dimensions never decide the scale, so the source's point pitch survives.
        left = round((origin[0] - screen_origin[0]) * scale) - 93
        top = round((origin[1] - screen_origin[1]) * scale) - 92
        width = round(cropped.width * scale)
        height = round(cropped.height * scale)
        cropped = cropped.resize((width, height), Image.Resampling.LANCZOS)
        patterns[str(code)] = (cropped, (left, top), "screens.png", box)

    # Night precipitation reuses the approved moon/cloud pixels and each daytime
    # precipitation strip. No anisotropic cloud scaling or guessed point grid.
    night_cloud = patterns['2n'][0]
    for code in [80, 81, 82, 85, 86]:
        day_icon, (dx, dy), _, _ = patterns[str(code)]
        full = Image.new('RGB', (280, 160)); full.paste(day_icon, (dx, dy))
        strip = full.crop((0, 120, 280, 160))
        strip = native_rgb(rgb565(strip), strip.size)
        # Resampling can leave dark fringes outside a snowflake. Apply the same
        # foreground floor as crop_pattern before packing the new composite.
        strip.putdata([p if max(p) > 24 else (0, 0, 0) for p in strip.get_flattened_data()])
        bounds = strip.getbbox(); assert bounds
        strip = strip.crop(bounds)
        gap = 160 - night_cloud.height - strip.height
        assert gap >= 1
        result = Image.new('RGB', (280, 160))
        result.paste(night_cloud, (patterns['2n'][1][0], 0))
        result.paste(strip, (bounds[0], night_cloud.height + gap))
        bounds = result.getbbox()
        patterns[str(code)+'n'] = (result.crop(bounds), bounds[:2], 'derived:2n+'+str(code), None)

    output = ['/* Generated by tools/gen_weather_artwork.py; approved source dots, no procedural shapes. */',
              '#include "weather_artwork.h"', '']
    entries = []
    for code, (image, (x, y), filename, box) in patterns.items():
        assert x >= 0 and y >= 0 and x + image.width <= 280 and y + image.height <= 160, (code, x, y, image.size)
        raw = rgb565(image)
        native = native_rgb(raw, image.size)
        expected = Image.new("RGB", (280, 160)); expected.paste(native, (x, y))
        expected.save(ART / "native" / f"{code}.png")
        packed = rle(raw)
        name = "wx_art_" + code
        output.append(f'static const uint8_t {name}_data[] = {{')
        for offset in range(0, len(packed), 20):
            output.append('    ' + ','.join(f'0x{v:02x}' for v in packed[offset:offset + 20]) + ',')
        output += ['};', f'static const lv_image_dsc_t {name} = {{',
                   '    .header.magic=LV_IMAGE_HEADER_MAGIC, .header.cf=LV_COLOR_FORMAT_RGB565,',
                   f'    .header.flags=LV_IMAGE_FLAGS_COMPRESSED, .header.w={image.width}, .header.h={image.height}, .header.stride={image.width * 2},',
                   f'    .data_size=sizeof {name}_data, .data={name}_data,', '};', '']
        number = int(code.rstrip("n")); night = code.endswith("n")
        entries.append(f'    {{ {number}, {str(night).lower()}, {x}, {y}, &{name} }},')
        metadata["icons"].append({"key": code, "source": filename, "crop": box, "offset": [x, y],
                                 "size": list(image.size), "rgb565_sha256": hashlib.sha256(raw).hexdigest(),
                                 "compressed_bytes": len(packed), "decoded_bytes": len(raw)})
    output += ['static const weather_artwork_t ARTWORK[] = {', *entries, '};',
               'const weather_artwork_t *weather_artwork_for(int code, bool is_day) {',
               '    bool night = !is_day && ((code >= 0 && code <= 2) || code == 80 || code == 81 || code == 82 || code == 85 || code == 86);',
               '    for (unsigned i=0; i<sizeof ARTWORK/sizeof ARTWORK[0]; ++i)',
               '        if (ARTWORK[i].code == code && ARTWORK[i].night == night) return &ARTWORK[i];',
               '    return NULL;', '}', '']
    (ROOT / "main/weather_artwork.c").write_text('\n'.join(output))
    (ART / "manifest.json").write_text(json.dumps(metadata, indent=2) + '\n')
    print(f'{len(patterns)} icons; compressed bytes={sum(i["compressed_bytes"] for i in metadata["icons"])}; largest decode={max(i["decoded_bytes"] for i in metadata["icons"])}')


if __name__ == "__main__":
    build()
