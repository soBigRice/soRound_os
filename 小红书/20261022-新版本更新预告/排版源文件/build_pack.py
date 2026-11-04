#!/usr/bin/env python3
"""Compose release-preview pages from preserved UI assets / 用保留的原生素材排版预告。"""
from pathlib import Path
from html import escape
import json

OUT = Path(__file__).resolve().parents[1]
SRC = OUT / '排版源文件'
PAGES = []


def image(group, name, label='', cls=''):
    path = OUT / '素材原图' / group / name
    assert path.is_file(), path
    return f'<figure class="{cls}"><img src="../素材原图/{group}/{name}" alt="{escape(label)}"><figcaption>{escape(label)}</figcaption></figure>'


def page(name, kicker, title, sub, content, cls='', note='原生 UI 渲染 · 画面数值为示例', extra=False):
    number = len(PAGES) + 1
    PAGES.append({'name': name, 'number': number, 'extra': extra, 'title': title.replace('<br>', ''), 'note': note})
    html = f'''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=1080"><title>{escape(name)}</title><link rel="stylesheet" href="style.css">
    <main class="poster {cls}" data-number="{number}"><header><div class="brand">soRound OS <span>明天 · 正式版见</span></div><div class="kicker">{kicker}</div><h1>{title}</h1><p class="sub">{sub}</p></header><div class="content">{content}</div><footer><span>{note}</span><span>{number:02d} / 10.23</span></footer></main></html>'''
    (SRC / f'{number:02d}-{name}.html').write_text(html)


def gallery(group, items, columns=3):
    return f'<div class="gallery cols-{columns}">' + ''.join(image(group, file, label) for file, label in items) + '</div>'


def points(items):
    return '<div class="points">' + ''.join(f'<div><b>{a}</b><p>{b}</p></div>' for a,b in items) + '</div>'


# 只调整外围排版，UI 内部文字、颜色与图形不重画。
# Compose around the screenshots; do not redraw text, colors or shapes inside the UI.
page('封面', '10.07 — 10.22 / 这半个月的小变化', '小圆屏，<br>又有新花样了。', '新表盘、小花园、唱片机，还有一些好玩的。',
     '<div class="cover-grid">' + image('04-设计八款','bauhaus.png') + image('09-随身应用','garden-grown-zh.png') + image('05-极简字体','stack.png') + image('09-随身应用','focus-running-zh.png') + '</div><div class="release"><span>10 月 23 日</span><b>正式版见。</b></div>', 'cover')

page('极简字体', '01 / 新表盘 · TYPE', '把时间，<br>放大一点。', '六种排版，让数字自己说话。',
     gallery('05-极简字体',[(f'{k}.png',v) for k,v in [('stack','STACK · 堆叠'),('air','AIR · 轻盈'),('offset','OFFSET · 错位'),('column','COLUMN · 双列'),('cut','CUT · 切分'),('edition','EDITION · 刊号')]]), 'gallery-six')

page('设计八款', '02 / 新表盘 · ATELIER', '每天，<br>换一种心情。', '留白、构成、唱片、月门……把喜欢的意象戴上。',
     gallery('04-设计八款',[(f'{k}.png',v) for k,v in [('mono','留白'),('bauhaus','构成'),('seven','电子'),('vinyl','唱片'),('prism','棱镜'),('metro','线路'),('moon-gate','月门'),('orrery','星仪')]]), 'gallery-nine', note='原生 UI 渲染 · 唱片、线路、月亮与星体为视觉意象')

page('STUDIO场景', '03 / 新表盘 · STUDIO', '时间，<br>也可以有画面。', '雷达、磁带、像素夜营；黑底上的九个小世界。',
     gallery('02-STUDIO黑底',[(f'{k}.png',v) for k,v in [('radar','雷达'),('tape','磁带'),('iris','光圈'),('maze','迷宫秒'),('fold','翻页'),('pixel-camp','像素夜营'),('horizon','地平线'),('tide','潮汐'),('growth','年轮')]]), 'gallery-nine')

page('图形表盘', '04 / 新表盘 · GRAPHIC', '让秒针，<br>换个走法。', '扇区、网格、花瓣、色带和二进制，都在认真计时。',
     gallery('03-图形表盘',[(f'{k}.png',v) for k,v in [('sector','扇区'),('solar','日轮'),('grid','网格'),('petal','花瓣'),('ribbon','色带'),('binary','二进制')]]), 'gallery-six')

page('运动图形', '05 / 新表盘 · MOTION', '时间，<br>跑起来了。', '把时、分、秒，变成圆环、轨道、阵列和进度带。',
     gallery('06-运动图形',[(f'{k}.png',v) for k,v in [('activity','律动'),('circuit','赛道'),('matrix','阵列'),('vector','矢量'),('duo','双环'),('stride','步幅')]]), 'gallery-six', note='原生 UI 渲染 · 图形表达时分秒，不代表健康或运动数据')

page('六款指针', '06 / 新表盘 · HAND', '还是指针，<br>也有新意思。', '刻度、弧线、数字、轨道、方框与圆点。',
     gallery('01-指针表盘',[(f'hand-{k}.png',v) for k,v in [('mark','刻度'),('arc','弧线'),('numeral','数字'),('orbit','轨道'),('frame','方框'),('dots','圆点')]]), 'gallery-six')

page('几何读时', '07 / 新表盘 · GEOMETRY', '如果没有数字，<br>时间会是什么样？', '坐标、支点、面积、合相与开合，试着用几何表达时间。',
     gallery('16-几何读时',[(f'{k}.png',v) for k,v in [('cross','坐标'),('balance','支点'),('hourglass','沙漏'),('quanta','方量'),('phase','合相'),('aperture','开合')]]), 'gallery-six', note='原生 UI 渲染 · 几何为艺术化读时表达')

icons = ''.join(image('12-实心图标',f'{k}.png',v) for k,v in [('weather','天气'),('pixels','方块'),('remote','遥控台'),('merit','木鱼')])
page('表盘入口与图标', '08 / 更顺手的入口', '喜欢哪一款，<br>直接换。', '表盘有了独立入口，实心图标也重新整理了一遍。',
     '<div class="duo">' + image('07-表盘入口','launcher-zh.png','独立表盘入口') + image('05-极简字体','selector-44-zh.png','分类与实时预览') + '</div><div class="icon-grid">' + icons + '</div><p class="caption">原设置入口保留 · 即选即存 · 中文 / English</p>', 'picker')

page('像素互动', '09 / 新玩法 · 方块', '手痒的时候，<br>拨两下。', '拖一拖会弹开，摇一摇会重排；玩完再一键重聚。',
     gallery('08-像素互动',[(f'pixels-{k}-zh.png',v) for k,v in [('initial','轻轻排好'),('touch','手指拨开'),('shake','摇一摇重排'),('palette','换一种配色')]],2), 'four', note='原生 UI 渲染 · 触摸、摇动与配色的示例状态')

page('掌心生态箱', '10 / 新应用 · 生态箱', '在掌心，<br>养一片小风景。', '浇一点水，看昼夜变化；每次专注结束，多一片新叶。',
     '<div class="hero">' + image('09-随身应用','garden-grown-zh.png') + '</div><div class="mini-row">' + image('09-随身应用','garden-ready-zh.png','日间') + image('09-随身应用','garden-water-zh.png','浇水') + image('09-随身应用','garden-night-zh.png','夜间') + '</div>', 'hero-page')

page('专注唱片机', '11 / 新应用 · 专注', '放一张唱片，<br>专心一会儿。', '25 分钟，配一点雨声、海浪或篝火声。',
     '<div class="hero">' + image('09-随身应用','focus-running-zh.png') + '</div><div class="mini-row">' + image('09-随身应用','focus-ready-zh.png','开始') + image('09-随身应用','focus-paused-zh.png','暂停 / 继续') + image('09-随身应用','focus-done-zh.png','完成，长出新叶') + '</div>', 'hero-page', note='原生 UI 渲染 · 音景为离线合成环境声')

page('电脑遥控台', '12 / 遥控台升级', '小圆屏，<br>也管点大事。', '移动鼠标、演示翻页、控制音乐，放在手边就能用。',
     '<div class="duo">' + image('10-遥控台','remote-mouse-zh.png','鼠标 · 轻点 / 滚动 / 拖拽') + image('10-遥控台','remote-slides-zh.png','演示 · 翻页与计时') + '</div><div class="music-row">' + image('10-遥控台','centered-controls/media-waiting.png','音乐三键') + '<div><b>上一曲<br>播放 / 暂停<br>下一曲</b><p>三键居中，留出更清楚的触摸区域。<br>长按音乐，调出音量控制。</p></div></div>', 'remote')

page('声音胶囊', '13 / 新应用 · 声音胶囊', '有些瞬间，<br>想留一点声音。', '录下、回放、整理，让一句话有个小小的容器。',
     '<div class="hero">' + image('09-随身应用','capsules-recording-zh.png') + '</div><div class="duo small-duo">' + image('09-随身应用','capsules-list-zh.png','录音列表 / 回放') + image('09-随身应用','capsules-delete-confirm-zh.png','二次确认删除') + '</div><p class="condition">使用条件：录音功能需先完成独立存储初始化。</p>', 'capsules', note='原生 UI 渲染 · 录音状态与波形为测试输入')

page('迷宫流体木鱼', '14 / 熟悉的玩法，也继续打磨', '认真玩，<br>也认真解压。', '三章十二关迷宫、重力粒子，还有新的敲击反馈。',
     '<div class="duo">' + image('01-指针表盘','maze-menu-zh.png','迷宫 · 自由选关') + image('01-指针表盘','maze-05-zh.png','固定关卡 · 重力滚球') + '</div><div class="duo">' + image('01-指针表盘','particles-zh.png','粒子流体 · 随倾斜流动') + image('14-木鱼','native-merit-fade.png','木鱼 · 敲击虚影') + '</div>', 'four', note='原生 UI 渲染 · 保留粒子流体玩法')

page('体验与官网', '15 / 看得到的变化，和看不见的功夫', '外表换新，<br>细节也慢慢磨。', '这半个月，也把日常使用里那些小地方重新捋了捋。',
     points([('少一点重复绘制','减少音乐页、录音页和迷宫静止时的无效刷新。'),('保存放到后台','设置、成长和音景偏好合并写入。'),('把稳定性继续补好','表盘绘制、启动自检、木鱼反馈与联网诊断继续打磨。')]) + '<div class="site-strip"><span>官网同步整理</span><b>完整表盘图鉴 · 分类浏览 · 中英文展示</b><p>每一款，都能点开看看。</p></div><div class="closing"><span>10 月 23 日</span><b>正式版，明天见。</b></div>', 'closing-page', note='新版本预告 · 2026.10.23 正式版见')

page('备选-地图概念', '备选素材 / 随身地图视觉探索', '下一张风景，<br>想装进圆里。', '月牙操作带、圆弧缩放、轻量工具入口，三种圆屏想象。',
     '<div class="map-main">' + image('设计留档/05-地图概念','crescent.png','月牙操作带') + '</div><div class="duo map-duo">' + image('设计留档/05-地图概念','orbit.png','圆弧缩放') + image('设计留档/05-地图概念','quiet-lens.png','轻量工具入口') + '</div>', 'map-page', note='AI 视觉概念 · 地理细节非真实输出 · 不列入明日功能承诺', extra=True)

(SRC / 'pages.json').write_text(json.dumps(PAGES,ensure_ascii=False,indent=2))
print(f'Prepared {len(PAGES)} pages / 已生成 {len(PAGES)} 页排版源文件。')
