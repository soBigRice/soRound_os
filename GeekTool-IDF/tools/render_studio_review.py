#!/usr/bin/env python3
"""Capture the real STUDIO renderer beside the approved SVG frames; remove temporary PPMs."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
from PIL import Image, ImageChops, ImageDraw, ImageFont, ImageStat

ROOT = Path(__file__).resolve().parents[1]
NAMES = ('RADAR', 'TAPE', 'IRIS', 'MAZE', 'FOLD', 'PIXEL CAMP', 'HORIZON', 'TIDE', 'GROWTH')


def rgb565(image):
    # 对照屏幕实际色深，不把RGB888到RGB565的量化误报为布局偏差。
    # Compare at the display's real depth instead of treating RGB888 quantization as a layout error.
    channels = image.convert('RGB').split()
    return Image.merge('RGB', (channels[0].point(lambda n: (n >> 3) * 255 // 31),
                              channels[1].point(lambda n: (n >> 2) * 255 // 63),
                              channels[2].point(lambda n: (n >> 3) * 255 // 31)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--faces', type=Path, required=True)
    parser.add_argument('--settings', type=Path)
    parser.add_argument('--out', type=Path, default=ROOT / 'artwork/watchfaces/studio-native')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    font = ImageFont.truetype(str(ROOT / 'artwork/watchfaces/fonts/Barlow-Regular.ttf'), 20)
    mask = Image.new('L', (466, 466))
    ImageDraw.Draw(mask).ellipse((2, 2, 464, 464), fill=255)
    receipt = {'fixture': '2026-10-20 10:08:36', 'renderer': 'production LVGL RGB565',
               'device_verified': False, 'reference': 'approved offline SVG, Chrome at 1x', 'faces': []}
    with tempfile.TemporaryDirectory(prefix='soround-studio-review-') as folder:
        subprocess.run([str(args.faces.resolve()), folder], check=True)
        if args.settings:
            subprocess.run([str(args.settings.resolve()), folder], check=True)
        for state, prefix in [('active', 'studio-reference-'), ('aod', 'studio-reference-aod-')]:
            reference = Image.open(args.out / f'reference-{state}.png').convert('RGB')
            assert reference.size == (1398, 1398)
            gallery = Image.new('RGB', (1494, 1674), '#151613')
            draw = ImageDraw.Draw(gallery)
            draw.text((24, 16), f'STUDIO / NATIVE LVGL / {state.upper()} / device acceptance pending', font=font, fill='#d8d7cd')
            pairs = [Image.new('RGB', (980, 1584), '#151613') for _ in range(3)]
            for i, name in enumerate(NAMES):
                frame = Image.open(Path(folder) / f'{prefix}{21+i}.ppm').convert('RGB')
                frame.save(args.out / f'{state}-{name.lower().replace(" ", "-")}.png')
                x, y = 16 + (i % 3) * 494, 62 + (i // 3) * 534
                gallery.paste(frame, (x, y))
                draw.text((x+233, y+478), f'{21+i:02d} / {name}', font=font, fill='#d8d7cd', anchor='mt')
                ref = reference.crop(((i % 3)*466, (i // 3)*466, (i % 3+1)*466, (i // 3+1)*466))
                row = i % 3
                pair = pairs[i // 3]
                pair.paste(rgb565(ref), (8, row*528+33))
                pair.paste(frame, (506, row*528+33))
                pd = ImageDraw.Draw(pair)
                pd.text((8, row*528+5), f'{name} / SVG at RGB565', font=font, fill='#d8d7cd')
                pd.text((506, row*528+5), 'NATIVE LVGL', font=font, fill='#d8d7cd')
                delta = ImageChops.difference(rgb565(ref), frame)
                mean = ImageStat.Stat(delta, mask).mean
                receipt['faces'].append({'name': name, 'state': state, 'mean_absolute_rgb_error': [round(v, 4) for v in mean]})
                # 真实缩略图角落必须保持黑色，避免曲线或背景漏出圆形。
                # Keep real preview corners black so backgrounds and curves cannot escape the circle.
                assert all(frame.getpixel(p) == (0, 0, 0) for p in [(0, 0), (465, 0), (0, 465), (465, 465)])
            gallery.save(args.out / f'{state}-overview.png')
            for i, series in enumerate(('instrument', 'play', 'nature')):
                pairs[i].save(args.out / f'compare-{state}-{series}.png')
        if args.settings:
            for face in (21, 29):
                for language in ('zh', 'en'):
                    Image.open(Path(folder) / f'face-{face}-{language}.ppm').save(args.out / f'selector-{face}-{language}.png')
    (args.out / 'comparison.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt))


if __name__ == '__main__':
    main()
