#!/usr/bin/env python3
"""Protect shipped CJK ink, advances and baseline when extending/cropping fonts."""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
BASELINE = ROOT / "tests/system/font_semantics.json"


def semantics(path):
    source = path.read_text()
    bitmap = re.search(r"glyph_bitmap\[\]\s*=\s*\{(.*?)\};", source, re.S).group(1)
    data = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", bitmap))
    names = ["bitmap_index", "adv_w", "box_w", "box_h", "ofs_x", "ofs_y"]
    pattern = r"\{" + r",\s*".join(r"\." + name + r"\s*=\s*(-?\d+)" for name in names) + r"\}"
    entries = [tuple(map(int, values)) for values in re.findall(pattern, source)]
    first = int(re.search(r"\.range_start\s*=\s*(\d+)", source).group(1))
    offsets = re.search(r"unicode_list_0\[\]\s*=\s*\{([^}]+)\}", source).group(1)
    codes = [first + int(value) for value in re.findall(r"\d+", offsets)]
    assert len(entries) == len(codes) + 1 and len(codes) > 300
    assert re.search(r"\.bpp\s*=\s*4\b", source)
    metrics = [int(re.search(r"\." + name + r"\s*=\s*(\d+)", source).group(1))
               for name in ("line_height", "base_line")]
    result = {"metrics": metrics, "glyphs": {}}
    for code, (index, advance, width, height, x, y) in zip(codes, entries[1:]):
        points = []
        for pixel in range(width * height):
            alpha = (data[index + pixel // 2] >> (4 if pixel % 2 == 0 else 0)) & 15
            if alpha:
                points.append([x + pixel % width, -y - height + pixel // width, alpha])
        result["glyphs"][str(code)] = hashlib.sha256(
            json.dumps([advance, points], separators=(",", ":")).encode()).hexdigest()
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--pin-from", type=Path, help="explicit pre-change font source")
    args = parser.parse_args()
    if args.pin_from:
        assert not BASELINE.exists(), "Do not overwrite shipped appearance to hide a failure"
        BASELINE.write_text(json.dumps(semantics(args.pin_from), indent=2) + "\n")
    expected = json.loads(BASELINE.read_text())
    actual = semantics(ROOT / "main/font_cn16.c")
    assert actual["metrics"] == expected["metrics"], "CJK font metrics changed"
    for code, digest in expected["glyphs"].items():
        assert actual["glyphs"].get(code) == digest, f"Shipped glyph changed: U+{int(code):04X}"
    print(f"{len(expected['glyphs'])} shipped CJK glyphs retain exact alpha, baseline position and advance; "
          f"{len(actual['glyphs']) - len(expected['glyphs'])} new glyphs.")
