#!/usr/bin/env python3
"""Run native fixtures, retain review PNGs, and remove intermediate raw/PPM files."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile
from PIL import Image, ImageDraw, ImageFont

ROOT=Path(__file__).resolve().parents[1]
THEMES=('TYPE','ORBIT','SHIFT')
KINDS=('DOTS','BOLD','RINGS','WEATHER','IMAGE')

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--faces',type=Path,required=True)
    parser.add_argument('--settings',type=Path,required=True)
    parser.add_argument('--out',type=Path,default=ROOT/'artwork/watchfaces/native')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    font=ImageFont.truetype(str(ROOT/'artwork/watchfaces/fonts/Barlow-Regular.ttf'),26)
    small=ImageFont.truetype(str(ROOT/'artwork/watchfaces/fonts/Barlow-Regular.ttf'),19)
    mask=Image.new('L',(466,466));ImageDraw.Draw(mask).ellipse((0,0,465,465),fill=255)
    with tempfile.TemporaryDirectory(prefix='soround-face-review-') as temporary:
        temp=Path(temporary)
        for folder in ('raw','faces','settings'): (temp/folder).mkdir()
        for theme in THEMES:
            image=Image.open(ROOT/f'artwork/watchfaces/{theme.lower()}.jpg').convert('RGB')
            rgb=image.tobytes()
            data=b''.join(struct.pack('<H',((rgb[i]>>3)<<11)|((rgb[i+1]>>2)<<5)|(rgb[i+2]>>3))
                          for i in range(0,len(rgb),3))
            (temp/f'raw/{theme}.rgb565').write_bytes(data)
        subprocess.run([str(args.faces.resolve()),str(temp/'faces'),str(temp/'raw')],check=True)
        subprocess.run([str(args.settings.resolve()),str(temp/'settings'),str(temp/'raw/TYPE.rgb565')],check=True)
        overview=Image.new('RGB',(2550,1860),'#171719');overview_draw=ImageDraw.Draw(overview)
        overview_draw.text((50,25),'15 WATCHFACES / NATIVE LVGL / 466 x 466',fill='#eeeeee',font=font)
        overview_draw.text((50,66),'Host-rendered data fixtures; device acceptance pending.',fill='#aaaaaf',font=small)
        for theme,name in enumerate(THEMES):
            board=Image.new('RGB',(1680,1220),'#171719');draw=ImageDraw.Draw(board)
            draw.text((50,30),name+' / NATIVE LVGL / 466 x 466',fill='#eeeeee',font=font)
            draw.text((50,73),'Fixtures: MON 05 OCT 2026, 10:08, 74%, 26 C. Device acceptance pending.',fill='#aaaaaf',font=small)
            overview_draw.text((50,112+theme*576),name,fill='#aaaaaf',font=small)
            for kind,xy in enumerate(((60,145),(607,145),(1154,145),(334,709),(881,709))):
                image=Image.open(temp/f'faces/face-{theme*5+kind:02d}.ppm').convert('RGB')
                board.paste(image,xy,mask);draw.text((xy[0]+233,xy[1]+486),KINDS[kind],fill='#cccccf',font=small,anchor='mt')
                pos=(50+kind*500,145+theme*576);overview.paste(image,pos,mask)
                overview_draw.text((pos[0]+233,pos[1]+482),KINDS[kind],fill='#cccccf',font=small,anchor='mt')
            board.save(args.out/f'{name.lower()}.png')
        overview.save(args.out/'overview.png')
        Image.open(temp/'settings/face-0-zh.ppm').save(args.out/'selector.png')
        for name in ('ssid-32-low-battery','ssid-32-theme-0','ssid-32-theme-1','ssid-32-theme-2',
                     'weather-negative','weather-unavailable','image-loading','custom-image-bright'):
            Image.open(temp/f'faces/{name}.ppm').save(args.out/f'{name}.png')
        aod=Image.new('RGB',(1050,610),'#171719');draw=ImageDraw.Draw(aod)
        draw.text((50,25),'IMAGE / ACTIVE + AOD / NATIVE LVGL',fill='#eeeeee',font=font)
        for i,state in enumerate(('face-04','aod-04')):aod.paste(Image.open(temp/f'faces/{state}.ppm'),(50+i*500,85),mask)
        aod.save(args.out/'aod.png')
    print('Review PNGs saved to',args.out,'; temporary raw/PPM files removed.')

if __name__=='__main__':main()
