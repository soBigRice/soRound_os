#!/usr/bin/env python3
"""Generate the network-title fallback using local PingFang + official lv_font_conv 1.5.3."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from fontTools.ttLib import TTCollection

parser=argparse.ArgumentParser()
parser.add_argument('--font-collection',type=Path,required=True)
parser.add_argument('--converter',type=Path,required=True)
parser.add_argument('--output',type=Path,default=Path('main/font_answer_cjk.c'))
# 保持答案字库默认值，让股票复用同一转换链。/ Preserve answer defaults while reusing conversion for stocks.
parser.add_argument('--size',type=int,default=32)
parser.add_argument('--font-name',default='font_answer_cjk_32')
args=parser.parse_args()
if not 8<=args.size<=64 or not args.font_name.isidentifier():
    parser.error('size must be 8..64 and font-name must be a C identifier')
symbols=[]
for first in range(0xb0,0xd8):
    for second in range(0xa1,0xff):
        try: symbols.append(bytes([first,second]).decode('gb2312'))
        except UnicodeDecodeError: pass
assert len(symbols)==3755
fonts=TTCollection(args.font_collection,lazy=True)
font=next(f for f in fonts.fonts if f['name'].getDebugName(1)=='PingFang SC' and f['name'].getDebugName(2)=='Regular')
with tempfile.TemporaryDirectory(prefix='wxesp32-answers-font-') as directory:
    folder=Path(directory);source=folder/'PingFangSC-Regular.ttf';output=folder/'font.c'
    font.save(source)
    subprocess.run([str(args.converter.resolve()),'--font',str(source),'--symbols',''.join(symbols),
                    '--size',str(args.size),'--bpp','2','--format','lvgl','--lv-font-name',args.font_name,
                    '--lv-include','lvgl.h','--no-kerning','--output',str(output)],check=True)
    content=output.read_text()
    content=f'// Generated with lv_font_conv 1.5.3; PingFang SC Regular, GB2312 level 1, {args.size}px/2bpp RLE.\n'+content[content.index('#'):]
    args.output.write_text(content.rstrip()+'\n')
fonts.close()
print(f'{args.output}: 3755 common CJK characters, {args.size}px, compressed 2bpp')
