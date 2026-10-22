#!/usr/bin/env python3
"""极简字体概念稿生成器，不接入固件。 / Minimal typography concepts, separate from firmware."""
from pathlib import Path
from html import escape
import json
import base64
from io import BytesIO
from fontTools import subset
from fontTools.ttLib import TTFont
ROOT=Path(__file__).resolve().parent
NAMES=[("stack","叠字","STACK"),("air","细字","AIR"),("offset","错位","OFFSET"),("column","分栏","COLUMN"),("cut","切字","CUT"),("edition","刊物","EDITION")]
def embedded_font(weight, filename):
    # SVG作为图片加载时不读取外部字体，嵌入OFL字体子集以保持真实字形。
    # SVG image documents cannot fetch external fonts; embed OFL subsets to preserve actual glyphs.
    font=TTFont(ROOT.parent/'fonts'/filename)
    subsetter=subset.Subsetter();subsetter.populate(unicodes=range(32,91));subsetter.subset(font)
    font.flavor='woff';buffer=BytesIO();font.save(buffer)
    encoded=base64.b64encode(buffer.getvalue()).decode('ascii')
    return f"@font-face{{font-family:Barlow;src:url(data:font/woff;base64,{encoded});font-weight:{weight}}}"
FONT_STYLE=''.join(embedded_font(weight,filename) for weight,filename in [(400,'Barlow-Regular.ttf'),(600,'Barlow-SemiBold.ttf'),(900,'Barlow-Black.ttf')])+'text{font-family:Barlow,sans-serif}'
def text(value,x,y,size=14,weight=400,color="#eeeae0",spacing=0):
    return f'<text x="{x}" y="{y}" text-anchor="middle" font-size="{size}" font-weight="{weight}" fill="{color}" letter-spacing="{spacing}">{escape(value)}</text>'
def line(x1,y1,x2,y2,color="#555",width=1):
    return f'<path d="M{x1} {y1}H{x2}" stroke="{color}" stroke-width="{width}"/>' if y1==y2 else f'<path d="M{x1} {y1}L{x2} {y2}" stroke="{color}" stroke-width="{width}"/>'
def face(key,hour="10",minute="08"):
    # 每款仅使用时间、日期和排版；图形不暗示额外传感器能力。
    # Only time, date and typography are shown; marks imply no additional sensor features.
    cream="#eeeae0";dim="#7d7e79";accent="#e15e46"
    if key=="stack":
        body=text("THU / 22 OCT",233,71,12,400,dim,3)+text(hour,233,224,202,900)+text(minute,233,389,202,900)+line(142,243,324,243,accent,3)
    elif key=="air":
        body=text("THU",233,131,12,400,dim,5)+text(hour+":"+minute,233,284,161,400,cream,-6)+text("22 / 10",233,342,14,400,dim,5)+line(216,379,250,379,"#b9b5a8",1)
    elif key=="offset":
        body=text(hour,189,225,215,600)+text(minute,289,379,176,400,"#b5b4ad",-3)+text("22 OCT / THU",121,322,11,400,dim,2)+'<circle cx="354" cy="157" r="6" fill="#e6a76b"/>'
    elif key=="column":
        body=text("H",156,91,12,400,dim,3)+text("M",307,91,12,400,dim,3)+line(233,107,233,373,"#30322f",1)
        for x,values,color,weight in [(157,hour,cream,600),(308,minute,"#a5b9ac",400)]:
            for y,value in zip([234,373],values): body+=text(value,x,y,166,weight,color)
        body+=text("THU 22",233,424,11,400,dim,3)
    elif key=="cut":
        body=text(hour,233,223,220,900)+text(minute,233,393,220,900)+line(103,170,364,170,"#000",4)+line(103,340,364,340,"#000",4)+text("22",233,433,13,600,accent,2)
    else:
        body=text("T I M E / 2 2",233,104,12,400,dim)+text(hour,176,300,200,900)+text(minute,350,282,88,400,"#d0c9b7",-2)+line(87,325,379,325,"#c9c4b7",1)+text("THURSDAY",171,361,12,400,dim,3)+text("OCT",350,361,12,400,dim,2)
    style=FONT_STYLE
    return f'<svg xmlns="http://www.w3.org/2000/svg" width="466" height="466" viewBox="0 0 466 466" role="img" aria-label="{key} {hour}:{minute}"><style>{style}</style><defs><clipPath id="screen"><circle cx="233" cy="233" r="233"/></clipPath></defs><g clip-path="url(#screen)"><circle cx="233" cy="233" r="233" fill="#000"/>{body}</g></svg>'
for key,cn,en in NAMES: (ROOT/(key+'.svg')).write_text(face(key))
manifest=[dict(key=key,cn=cn,en=en) for key,cn,en in NAMES]
# 预览页不使用外部服务，所有字体均来自已有OFL资源。
# The preview stays local and uses the project's existing OFL fonts without external services.
# 同步更新整组地址，让浏览器管理图片加载；快速切换不会抛出旧解码任务的取消错误。
# Update the whole set synchronously and let the browser load images; rapid changes create no rejected decode promises.
page="""<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>soRound · 极简字体探索</title><style> *{box-sizing:border-box}body{margin:0;background:#17191a;color:#eae9e3;font:16px system-ui,sans-serif}main{max-width:1518px;margin:auto;padding:30px 24px}header{display:flex;justify-content:space-between;align-items:center;gap:20px;margin-bottom:32px}h1{font-size:22px;letter-spacing:2px;margin:0 0 8px}p{color:#9c9e99;font-size:13px;margin:0}nav{display:flex;gap:8px}button{font:inherit;background:#252829;color:#deded7;border:1px solid #454847;border-radius:24px;padding:10px 18px;cursor:pointer}button[aria-pressed=true]{background:#e6e1d5;color:#17191a}button:focus-visible{outline:2px solid #e7a26f;outline-offset:3px}.grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:30px 24px}figure{margin:0;text-align:center}img{display:block;width:100%;height:auto;aspect-ratio:1;border-radius:50%;max-width:466px;margin:auto}figcaption{padding-top:14px;font-size:15px;letter-spacing:3px}small{color:#999d97;font-size:11px;letter-spacing:1px;margin-left:9px}@media(max-width:800px){.grid{grid-template-columns:repeat(2,minmax(0,1fr))}header{align-items:start;flex-direction:column}main{padding:24px 16px}nav{flex-wrap:wrap}h1{font-size:20px}}@media(prefers-reduced-motion:no-preference){button{transition:background .15s}}</style><main><header><div><h1>TYPE EXPLORATIONS / 极简大字</h1><p>六款排版概念 · 466px 圆屏 · 尚未接入固件或官网</p></div><nav aria-label="样例时间"><button data-time="10:08" aria-pressed="true">10:08</button><button data-time="00:00" aria-pressed="false">00:00</button><button data-time="23:59" aria-pressed="false">23:59</button></nav></header><div class="grid">{{cards}}</div></main><script>const designs={{manifest}};document.querySelectorAll('[data-time]').forEach(button=>button.addEventListener('click',()=>{const time=button.dataset.time;document.querySelectorAll('[data-time]').forEach(item=>item.setAttribute('aria-pressed',String(item===button)));designs.forEach(design=>{const image=document.querySelector('[data-key="'+design.key+'"]');image.src='states/'+design.key+'-'+time.replace(':','')+'.svg';image.alt=design.en+' '+time;});}));</script></html>"""
(ROOT/'states').mkdir(exist_ok=True)
for key,cn,en in NAMES:
    for stamp in ["10:08","00:00","23:59"]:
        h,m=stamp.split(':');(ROOT/'states'/f'{key}-{h}{m}.svg').write_text(face(key,h,m).replace("../fonts/","../../fonts/"))
cards=''.join(f'<figure><img data-key="{key}" src="{key}.svg" alt="{en} 10:08"><figcaption>{en}<small>{cn}</small></figcaption></figure>' for key,cn,en in NAMES)
(ROOT/'index.html').write_text(page.replace('{{cards}}',cards).replace('{{manifest}}',json.dumps(manifest)))
print('Generated six local SVG typography concepts with three sample times.')
