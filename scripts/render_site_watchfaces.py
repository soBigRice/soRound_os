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
    parser.add_argument("--review", type=Path, help="Optional folder for selected active/AOD renders")
    parser.add_argument("--review-from", type=int, default=30, help="First review ID, inclusive")
    parser.add_argument("--review-to", type=int, default=36, help="Last review ID, exclusive")
    args = parser.parse_args()
    catalog = json.loads((ROOT / "site/watchfaces.json").read_text())
    studio = catalog["studio"]["kinds"]
    if args.review:
        if not 21 <= args.review_from < args.review_to <= 21 + len(studio):
            parser.error("Review IDs must be a nonempty range within STUDIO")
        args.review.mkdir(parents=True, exist_ok=True)
        count = args.review_to - args.review_from
        columns = 4 if count > 6 else 3
        board = Image.new("RGB", (columns * 494 + 12, ((count + columns - 1) // columns) * 548 + 24), "#151719")
        boundaries = {state: Image.new("RGB", board.size, "#151719") for state in ("midnight", "late")} if args.review_from >= 36 else {}
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
            if args.review and args.review_from <= index < args.review_to:
                active.save(args.review / f"{face['key']}.png")
                aod = Image.open(Path(temporary) / f"studio-reference-aod-{index:02d}.ppm")
                aod.save(args.review / f"{face['key']}-aod.png")
                number = index - args.review_from
                x, y = 16 + number % columns * 494, 24 + number // columns * 548
                board.paste(active, (x, y))
                draw.text((x + 233, y + 484), f"{index:02d} / {face['key'].upper()}", font=font, fill="#dedfd9", anchor="mt")
                for state, boundary_board in boundaries.items():
                    frame = Image.open(Path(temporary) / f"atelier-{index:02d}-{state}.ppm").convert("RGB")
                    boundary_board.paste(frame, (x, y))
                    ImageDraw.Draw(boundary_board).text((x + 233, y + 484), f"{index:02d} / {face['key'].upper()}", font=font, fill="#dedfd9", anchor="mt")
    if args.review:
        board.save(args.review / "overview.png")
        for state, boundary_board in boundaries.items():
            boundary_board.save(args.review / f"{state}-overview.png")
    print(f"Exported {len(studio)} native STUDIO faces; temporary PPMs removed.")


if __name__ == "__main__":
    main()
