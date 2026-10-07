#!/usr/bin/env python3
"""Export website PNGs from real 466px LVGL app fixtures; raw captures remain temporary."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
CAPTURES = {
    "weather": "forecast-hero-zh", "settings": "settings-home-zh",
    "level": "level-flat-zh", "audio": "audio-voice-zh", "dice": "coin-3-dice-zh",
    "answers": "answers-first-zh", "system": "overview-zh", "wifi": "wifi-connected-zh",
    "remote": "remote-media-zh", "merit": "merit-zh", "zodiac": "zodiac-daily-zh",
    "ota": "wxesp32-ota-download-zh",
    **{name: name for name in ("calendar", "countdown", "stopwatch", "maze", "fluid", "i2c", "twin")},
}
TARGETS = ("weather_ui_tests", "settings_level_tests", "audio_ui_tests", "dice_ui_tests",
           "answers_ui_tests", "system_ui_tests", "wifi_ui_tests", "remote_ui_tests",
           "merit_ui_tests", "zodiac_ui_tests", "ota_ui_tests")


def export(directory, language):
    output = ROOT / "site/assets/apps"
    if language == "en":
        output /= "en"
    output.mkdir(parents=True, exist_ok=True)
    for name, capture in CAPTURES.items():
        if capture.endswith("-zh"):
            capture = capture[:-3] + "-" + language
        with Image.open(directory / (capture + ".ppm")) as image:
            if image.size != (466, 466):
                raise ValueError(f"{name}: expected native 466 × 466")
            # Format conversion only: no resizing, redraw, or replacement of firmware pixels.
            image.save(output / (name + ".png"), optimize=True)
    print(f"Exported {len(CAPTURES)} native app PNGs to {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--language", choices=("zh", "en"), default="zh")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--build-dir", type=Path, help="CMake build of scripts/site_native")
    group.add_argument("--captures", type=Path, help="Reuse already verified native PPM exports")
    args = parser.parse_args()
    if args.captures:
        export(args.captures, args.language)
        return
    build = args.build_dir.resolve()
    with tempfile.TemporaryDirectory(prefix="soround-site-captures-") as temporary:
        directory = Path(temporary)
        for target in TARGETS:
            subprocess.run([str(build / "host" / target), str(directory)], check=True,
                           env=dict(os.environ, OTA_CAPTURE_DIR=str(directory)))
        command = [str(build / "site_native"), str(directory)]
        if args.language == "en":
            command.append("--english")
        subprocess.run(command, check=True)
        export(directory, args.language)


if __name__ == "__main__":
    main()
