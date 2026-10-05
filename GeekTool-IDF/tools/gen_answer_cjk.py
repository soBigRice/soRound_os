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
args=parser.parse_args()
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
                    '--size','32','--bpp','2','--format','lvgl','--lv-font-name','font_answer_cjk_32',
                    '--lv-include','lvgl.h','--no-kerning','--output',str(output)],check=True)
    content=output.read_text()
    content='// Generated with lv_font_conv 1.5.3; PingFang SC Regular, GB2312 level 1, 32px/2bpp RLE.\n'+content[content.index('#'):]
    args.output.write_text(content.rstrip()+'\n')
fonts.close()
print(f'{args.output}: 3755 common CJK characters, 32px, compressed 2bpp')
