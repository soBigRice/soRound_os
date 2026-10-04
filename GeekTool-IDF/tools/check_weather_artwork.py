#!/usr/bin/env python3
"""Compare real LVGL icon framebuffer pixels to the pinned native artwork."""
from pathlib import Path
import argparse
import subprocess
import tempfile
import hashlib
import json
from PIL import Image, ImageChops


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
