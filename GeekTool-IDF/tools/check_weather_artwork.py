#!/usr/bin/env python3
"""Compare real LVGL icon framebuffer pixels to the pinned native artwork."""
from pathlib import Path
import argparse
import subprocess
import tempfile
import hashlib
import json
import re
from PIL import Image, ImageChops


def font_semantic_hash(path):
    """All glyph alpha values/positions relative to baseline, advances and font metrics.

    Transparent padding is intentionally excluded; visible content cannot change.
    """
    source = path.read_text()
    bitmap = re.search(r"bitmap\[\]\s*=\s*\{(.*?)\};", source, re.S).group(1)
    data = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", bitmap))
    entries = re.findall(
        r"\.bitmap_index=(\d+),\s*\.adv_w=(\d+),\s*\.box_w=(\d+),\s*\.box_h=(\d+),\s*\.ofs_x=(-?\d+),\s*\.ofs_y=(-?\d+)",
        source,
    )
    assert len(entries) > 100 and re.search(r"\.bpp=4\b", source)
    metrics = [int(re.search(rf"\.{key}=(\d+)", source).group(1)) for key in ("line_height", "base_line")]
    unicode = re.search(r"uint16_t unicode\[\]\s*=\s*\{([^}]*)\}", source).group(1)
    result = [metrics, unicode]
    for entry in entries:
        index, advance, width, height, offset_x, offset_y = map(int, entry)
        points = []
        for k in range(width * height):
            value = (data[index + k // 2] >> (4 if k % 2 == 0 else 0)) & 15
            if value:
                points.append([offset_x + k % width, -offset_y - height + k // width, value])
        result.append([advance, points])
    return hashlib.sha256(json.dumps(result, separators=(",", ":")).encode()).hexdigest()


def compare(renders, lvgl_version):
    baseline = Path(__file__).resolve().parents[1] / "artwork/weather/native"
    compared = 0
    failed = []
    for reference in sorted(baseline.glob("*.png")):
        key = reference.stem
        code, period = (key[:-1], "night") if key.endswith("n") else (key, "day")
        expected = Image.open(reference).convert("RGB")
        for lang in ("en", "zh"):
            actual = Image.open(renders / f"code-{code}-{period}-{lang}-icon.ppm").convert("RGB")
            difference = ImageChops.difference(expected, actual)
            if difference.getbbox():
                failed.append(f"{key}/{lang}: {difference.getbbox()}")
            compared += 1
    assert compared == 74, f"Missing pinned reference: {compared} comparisons"
    assert not failed, "Artwork/framebuffer mismatch:\n" + "\n".join(failed)
    print(f"{compared} exact RGB565 framebuffer comparisons passed (37 icons, English/Chinese).")
    # Renderer versions have different rasterization. Each baseline predates the scroll change.
    baseline_name = {"9.5.0": "first_screen.sha256.json", "9.6.0": "first_screen_lvgl96.sha256.json"}[lvgl_version]
    first_screen = Path(__file__).resolve().parents[1] / "tests/weather" / baseline_name
    pinned = json.loads(first_screen.read_text())
    for name, digest in pinned.items():
        actual = hashlib.sha256((renders / name).read_bytes()).hexdigest()
        assert actual == digest, f"Existing first-screen pixels changed: {name}"
    print(f"{len(pinned)} exact full first-screen framebuffer comparisons passed.")
    root = Path(__file__).resolve().parents[1]
    fonts = json.loads((root / "tests/weather/font_semantics.sha256.json").read_text())
    for name, digest in fonts.items():
        assert font_semantic_hash(root / "main" / name) == digest, f"Weather glyph appearance/metrics changed: {name}"
    print("All 324 weather glyphs retain exact alpha, baseline position and advance after lossless cropping.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("renders", nargs="?", type=Path, help="existing framebuffer directory")
    parser.add_argument("--render-with", type=Path, help="weather_ui_tests executable")
    parser.add_argument("--lvgl-version", default="9.5.0", choices=("9.5.0", "9.6.0"), help="renderer used for an existing directory")
    args = parser.parse_args()
    if bool(args.renders) == bool(args.render_with):
        parser.error("provide a framebuffer directory or --render-with executable")
    if args.render_with:
        with tempfile.TemporaryDirectory(prefix="weather-artwork-") as directory:
            subprocess.run([str(args.render_with), directory], check=True)
            version = subprocess.check_output([str(args.render_with), "--lvgl-version"], text=True).strip()
            compare(Path(directory), version)
    else:
        compare(args.renders, args.lvgl_version)
