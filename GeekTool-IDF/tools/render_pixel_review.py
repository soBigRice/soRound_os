#!/usr/bin/env python3
"""Export the real pixel app LVGL framebuffer; remove temporary PPM captures."""
import argparse
from pathlib import Path
import subprocess
import tempfile

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
STATES = (
    ('initial', 'Initial field'),
    ('touch', 'Real pointer drag'),
    ('shake', 'IMU shake / one burst'),
    ('palette', 'Palette button'),
    ('regroup', 'Regroup button'),
    ('noimu', 'No IMU / touch available'),
    ('read-failure', 'IMU read failure / touch available'),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tests', type=Path, required=True,
                        help='Built native pixel_tests executable')
    parser.add_argument('--out', type=Path, default=ROOT / 'artwork/pixels/native')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    font = ImageFont.truetype(str(ROOT / 'artwork/watchfaces/fonts/Barlow-Regular.ttf'), 20)
    with tempfile.TemporaryDirectory(prefix='soround-pixel-review-') as folder:
        subprocess.run([str(args.tests.resolve()), folder], check=True)
        gallery = Image.new('RGB', (1004, len(STATES) * 520 + 86), '#08080b')
        draw = ImageDraw.Draw(gallery)
        draw.text((24, 18), 'NATIVE LVGL / 466 x 466 / device acceptance pending',
                  fill='#b3b3bc', font=font)
        for row, (state, caption) in enumerate(STATES):
            for col, language in enumerate(('en', 'zh')):
                name = f'pixels-{state}-{language}'
                with Image.open(Path(folder) / f'{name}.ppm') as raw:
                    if raw.size != (466, 466):
                        raise ValueError(f'{name}: expected 466 x 466, got {raw.size}')
                    image = raw.convert('RGB')
                image.save(args.out / f'{name}.png')
                x, y = 24 + col * 490, 66 + row * 520
                gallery.paste(image, (x, y))
                draw.text((x + 233, y + 480), f'{language.upper()} / {caption}',
                          fill='#b3b3bc', font=font, anchor='mt')
        gallery.save(args.out / 'pixels-review.png')
    print('Saved native review PNGs to', args.out, '; temporary PPMs removed.')


if __name__ == '__main__':
    main()
