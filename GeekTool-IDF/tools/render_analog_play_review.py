#!/usr/bin/env python3
"""Capture the production LVGL renderers; keep PNGs and remove temporary PPMs."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
HANDS = ('MARK', 'ARC', 'NUMERAL', 'ORBIT', 'FRAME', 'DOTS')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--play', type=Path, required=True)
    parser.add_argument('--settings', type=Path, required=True)
    parser.add_argument('--out', type=Path, default=ROOT/'artwork/analog-play/native')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    font = ImageFont.truetype(str(ROOT/'artwork/watchfaces/fonts/Barlow-Regular.ttf'), 22)
    with tempfile.TemporaryDirectory(prefix='soround-analog-play-review-') as folder:
        temp = Path(folder)
        subprocess.run([str(args.play.resolve()), folder], check=True)
        subprocess.run([str(args.settings.resolve()), folder], check=True)

        def frame(name, target=None):
            image = Image.open(temp/f'{name}.ppm').convert('RGB')
            if target:
                image.save(args.out/f'{target}.png')
            return image

        def gallery(entries, cols, target, title):
            rows = (len(entries)+cols-1)//cols
            image = Image.new('RGB', (cols*490+24, rows*526+84), '#08080b')
            draw = ImageDraw.Draw(image)
            draw.text((24, 18), title, fill='#b3b3bc', font=font)
            for i, (source, caption) in enumerate(entries):
                x, y = 24+i % cols*490, 66+i//cols*526
                image.paste(frame(source), (x, y))
                draw.text((x+233, y+480), caption, font=font, fill='#b3b3bc', anchor='mt')
            image.save(args.out/f'{target}.png')

        for i, name in enumerate(HANDS):
            frame(f'hand-{i:02d}', f'hand-{name.lower()}')
        gallery([(f'hand-{i:02d}', f'HAND / {name}') for i, name in enumerate(HANDS)],
                3, 'hands', 'NATIVE LVGL / 466 x 466 / device acceptance pending')
        for name in ('particles-zh', 'ink-zh', 'maze-menu-zh', 'maze-05-zh', 'maze-win-zh'):
            frame(name, name)
        frame('face-15-zh', 'selector')
        gallery([('particles-zh', 'PARTICLES / original mode'), ('ink-zh', 'INK / real pointer stirring')],
                2, 'fluid', 'Native app content / launcher header omitted / device acceptance pending')
        gallery([('maze-menu-zh', '12 FIXED LEVELS'), ('maze-05-zh', '05 / 5 x 5'), ('maze-win-zh', 'COMPLETE / replay or choose next')],
                3, 'maze', 'Native app content / launcher header omitted / device acceptance pending')
    print('Saved native review PNGs to', args.out, '; temporary PPMs removed.')


if __name__ == '__main__':
    main()
