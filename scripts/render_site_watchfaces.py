#!/usr/bin/env python3
"""Export STUDIO PNGs from the real host renderer; remove intermediate PPMs automatically."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--faces", type=Path, required=True)
    parser.add_argument("--review", type=Path, help="Optional folder for six new active/AOD renders")
    args = parser.parse_args()
    catalog = json.loads((ROOT / "site/watchfaces.json").read_text())
    studio = catalog["studio"]["kinds"]
    if args.review:
        args.review.mkdir(parents=True, exist_ok=True)
        board = Image.new("RGB", (1494, 1120), "#151719")
        draw = ImageDraw.Draw(board)
        font = ImageFont.truetype(str(ROOT / "GeekTool-IDF/artwork/watchfaces/fonts/Barlow-Regular.ttf"), 22)
    with tempfile.TemporaryDirectory(prefix="soround-native-faces-") as temporary:
        subprocess.run([str(args.faces.resolve()), temporary], check=True)
        for kind, face in enumerate(studio):
            index = 21 + kind
            # 格式转换保留466px原生像素，不以网页重绘替代固件画面。
            # Preserve native 466px pixels during conversion instead of redrawing firmware UI in the browser.
            active = Image.open(Path(temporary) / f"studio-reference-{index:02d}.ppm").convert("RGB")
            assert active.size == (466, 466)
            active.save(ROOT / "site/assets" / face["file"])
            if args.review and face.get("preview"):
                active.save(args.review / f"{face['key']}.png")
                aod = Image.open(Path(temporary) / f"studio-reference-aod-{index:02d}.ppm")
                aod.save(args.review / f"{face['key']}-aod.png")
                number = kind - 9
                x, y = 16 + number % 3 * 494, 24 + number // 3 * 548
                board.paste(active, (x, y))
                draw.text((x + 233, y + 484), f"{index:02d} / {face['key'].upper()}", font=font, fill="#dedfd9", anchor="mt")
    if args.review:
        board.save(args.review / "overview.png")
    print(f"Exported {len(studio)} native STUDIO faces; temporary PPMs removed.")


if __name__ == "__main__":
    main()
