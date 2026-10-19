#!/usr/bin/env python3
"""对照用户选定稿检查真实LVGL像素。 / Compare native LVGL pixels with the approved artwork."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / "artwork/launcher/icon-design-20261019/selected"


def visible_bounds(image):
    # 黑底上的可见墨色，不把透明留白或按钮边框算入尺寸。
    # Measure visible ink on black, excluding transparent padding and the button frame.
    return image.convert("RGB").convert("L").point(lambda v: 255 if v > 24 else 0).getbbox()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--render-with", type=Path, required=True)
    args = parser.parse_args()
    icons = json.loads((ART / "manifest.json").read_text())["icons"]
    references = [ART / f"{icon['id']}.png" for icon in icons]
    for name in ("sprout", "disc-3", "mic"):
        icons.append({"id": name})
        references.append(ART.parent / "additional" / f"{name}.png")
    with tempfile.TemporaryDirectory(prefix="launcher-artwork-") as temp:
        subprocess.run([str(args.render_with.resolve()), temp, "--verify"], check=True, timeout=60,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        worst_error = 0
        for index, icon in enumerate(icons):
            rgba = Image.open(references[index]).convert("RGBA")
            reference = Image.new("RGB", rgba.size)
            reference.paste(rgba, mask=rgba.getchannel("A"))
            bounds = visible_bounds(reference)
            assert bounds is not None
            assert min(bounds[0], bounds[1], 148-bounds[2], 148-bounds[3]) >= 10, icon["id"]
            assert 112 <= max(bounds[2]-bounds[0], bounds[3]-bounds[1]) <= 119, icon["id"]
            for variant, left, top, scale in [("static",159,143,1), ("fade",159,143,.5), ("shift",122,166,1)]:
                image = Image.open(Path(temp) / f"verify-{variant}-{index:02d}-zh.ppm").convert("RGB")
                actual = image.crop((left,top,left+148,top+148))
                expected = reference if scale == 1 else reference.point(lambda v: round(v*scale))
                error = max(high for low, high in ImageChops.difference(actual,expected).getextrema())
                # RGB565量化和两次A8混合允许最多两级色阶；一像素错位会远超此界限。
                # Allow at most two RGB565 quantization steps; a one-pixel shift exceeds this bound.
                assert error <= 17, f"{icon['id']}/{variant}: pixel error {error}"
                worst_error = max(worst_error,error)
                actual_bounds = visible_bounds(actual)
                expected_bounds = visible_bounds(expected)
                assert actual_bounds and expected_bounds
                assert max(abs(a-b) for a,b in zip(actual_bounds,expected_bounds)) <= 1, icon["id"]
        print(f"PASS: {len(icons)} icons x 3 native states; size, origin, identity and fade; max RGB error {worst_error}/255")


if __name__ == "__main__":
    main()
