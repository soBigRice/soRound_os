#!/usr/bin/env python3
"""Compare real LVGL icon framebuffer pixels to the pinned native artwork."""
from pathlib import Path
import argparse
import subprocess
import tempfile
from PIL import Image, ImageChops


def compare(renders):
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


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("renders", nargs="?", type=Path, help="existing framebuffer directory")
    parser.add_argument("--render-with", type=Path, help="weather_ui_tests executable")
    args = parser.parse_args()
    if bool(args.renders) == bool(args.render_with):
        parser.error("provide a framebuffer directory or --render-with executable")
    if args.render_with:
        with tempfile.TemporaryDirectory(prefix="weather-artwork-") as directory:
            subprocess.run([str(args.render_with), directory], check=True)
            compare(Path(directory))
    else:
        compare(args.renders)
