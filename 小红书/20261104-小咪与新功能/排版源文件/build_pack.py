#!/usr/bin/env python3
"""用原生界面生成本地图文源稿 / Build local social layouts from native UI captures."""
from pathlib import Path
from html import escape
import hashlib
import json
import shutil

SOURCE = Path(__file__).resolve().parent
OUT = SOURCE.parent
ROOT = OUT.parents[1]
ASSETS = OUT / '素材原图'
ASSETS.mkdir(exist_ok=True)
(OUT / '发布配图').mkdir(exist_ok=True)

# 只复制原图，不重绘界面；行情截图选用当前 A 版。/ Preserve pixels and use the current A-layout captures.
FILES = {
    'mimi-awake': 'GeekTool-IDF/artwork/mimi/fullscreen-native/expression-06-zh.png',
    'mimi-look': 'GeekTool-IDF/artwork/mimi/fullscreen-native/expression-00-zh.png',
    'mimi-sleepy': 'GeekTool-IDF/artwork/mimi/fullscreen-native/expression-13-zh.png',
    'mimi-wink': 'GeekTool-IDF/artwork/mimi/fullscreen-native/motion-02-wink-zh.png',
    'mimi-egg': 'GeekTool-IDF/artwork/mimi/fullscreen-native/motion-08-egg-zh.png',
    'mimi-hexagon': 'GeekTool-IDF/artwork/mimi/fullscreen-native/motion-09-hexagon-zh.png',
    'mimi-play': 'GeekTool-IDF/artwork/mimi/fullscreen-native/motion-10-play-zh.png',
    'mimi-orbit': 'GeekTool-IDF/artwork/mimi/fullscreen-native/motion-11-orbit-zh.png',
    'mimi-sleep': 'GeekTool-IDF/artwork/mimi/fullscreen-native/motion-07-sleep-zh.png',
    'stocks-overview': 'GeekTool-IDF/build/flash-records/stocks-A-20261103/preview/watchlist-overview-zh.png',
    'stocks-detail': 'GeekTool-IDF/build/flash-records/stocks-A-20261103/preview/watchlist-detail-zh.png',
    'stocks-failure': 'GeekTool-IDF/build/flash-records/stocks-A-20261103/preview/watchlist-failure-zh.png',
    'stocks-editor': 'GeekTool-IDF/build/flash-records/stocks-A-20261103/preview/watchlist-editor-numeric-zh.png',
    'demo-watchface': 'site/assets/studio/active-bauhaus.png',
    'demo-weather': 'site/assets/apps/weather.png',
    'demo-maze': 'site/assets/apps/maze.png',
}
manifest = []
for name, rel in FILES.items():
    src = ROOT / rel
    if not src.is_file():
        raise FileNotFoundError(src)
    target = ASSETS / f'{name}.png'
    shutil.copy2(src, target)
    manifest.append({'name': name, 'source': rel, 'copy': str(target.relative_to(OUT)),
                     'sha256': hashlib.sha256(target.read_bytes()).hexdigest()})
(OUT / '素材清单.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')

def shot(name, label='', cls=''):
    return (f'<figure class="shot {cls}"><img src="../素材原图/{name}.png" alt="{escape(label or name)}">'
            + (f'<figcaption>{escape(label)}</figcaption>' if label else '') + '</figure>')

PAGES = []
def page(name, section, title, sub, content, cls='', note='原生界面排版预览 · 本地开发版，尚未公开发布'):
    index = len(PAGES) + 1
    filename = f'{index:02d}-{name}'
    PAGES.append({'filename': filename, 'title': title.replace('<br>', ''), 'note': note})
    document = f'''<!doctype html><html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=1080"><title>{escape(name)} · soRound OS</title>
<link rel="stylesheet" href="style.css"></head><body>
<main class="poster {cls}">
<header><div class="brand"><b><i></i> soRound OS</b><span>sobigrice / 开源小圆屏</span></div>
<div class="section"><span>{escape(section)}</span><span>开发进展 · 2026.11</span></div>
<h1>{title}</h1><p class="sub">{sub}</p></header>
<div class="content">{content}</div>
<footer><span>{escape(note)}</span><b>{index:02d} / 08</b></footer>
</main></body></html>'''
    (SOURCE / f'{filename}.html').write_text(document)

page('封面', '给桌面，添一点小表情', '我给小圆屏<br>养了只小咪', '点一下、划一下，这个白团子会回应。',
     '<div class="cover-hero">' + shot('mimi-awake') + '</div>'
     '<div class="cover-strip"><span>这次还加了</span><b>自选行情 <em>＋</em> 自动演示</b></div>', 'cover')

page('离线小咪', '01 / 新朋友 · 小咪', '不用联网，<br>也能陪你玩。', '没有账号，打开就是一只白团黑眼睛的小宠物。',
     '<div class="single pet-single">' + shot('mimi-look') + '</div>'
     '<div class="stats"><div><b>25<span>套</span></b><p>眼睛表情</p></div>'
     '<div><b>14<span>段</span></b><p>身体动作</p></div></div>'
     '<p class="caption">眨眨眼、看向你，闲着也会自己动一动。</p>')

page('身体动作', '02 / 小咪 · 不只会眨眼', '小白团，<br>也有好多小动作。', '从轻轻眨眼，到变形和彩环，都装进这个圆里。',
     '<div class="gallery four">' + ''.join(shot(n, t) for n, t in [
         ('mimi-wink', '眨一下眼'), ('mimi-egg', '变成小蛋'),
         ('mimi-hexagon', '变成多边形'), ('mimi-orbit', '绕起彩色圆环')]) + '</div>', 'motion-page')

page('触摸与犯困', '03 / 小咪 · 一点小互动', '不碰它，<br>还会自己犯困。', '放着放着就睡着了，碰一下又醒过来。',
     '<div class="gallery two sleep-pair">' + shot('mimi-awake', '醒着，看看你') + shot('mimi-sleep', '困了，缩成小团') + '</div>'
     '<div class="steps"><div><span>轻点</span><b>换一套眼睛表情</b></div>'
     '<div><span>滑动</span><b>触发下一段身体动作</b></div>'
     '<div><span>长按</span><b>叫出返回按钮</b></div></div>')

page('自选行情', '04 / 新工具 · 自选行情', '手边一瞥，<br>看看自选行情。', '价格、涨跌和时间，放在同一个自选列表里。',
     '<div class="single stock-single">' + shot('stocks-overview') + '</div>'
     '<div class="tags"><span>沪深 / 港股 / 美股</span><span>最多 8 只自选</span><span>滚动查看</span></div>',
     'stocks-page', '原生界面 · 价格为固定样例，行情源可能延迟 · 开发中')

page('行情状态', '05 / 自选行情 · 让信息说清楚', '价格要看，<br>时间和状态也要看。', '看清是哪只、什么币种、什么时候的报价。',
     '<div class="gallery two stock-pair">' + shot('stocks-detail', '单只详情 / 来源时间') + shot('stocks-failure', '查询失败 / 保留旧行情') + '</div>'
     '<div class="stock-notes"><div><b>整份自选自动刷新</b><p>屏幕外的股票，也会一起查询。</p></div>'
     '<div><b>单只失败，单独标记</b><p>保留上次报价和时间，不把旧数据当新行情。</p></div>'
     '<div><b>自己添加，也能移出</b><p>先选市场，再输入股票代码。</p></div></div>',
     '', '原生界面 · 报价与失败状态为固定样例 · 行情源可能延迟')

page('自动演示', '06 / 给拍摄多一个帮手', '拍小圆屏，<br>不用一直手动点了。', '新增自动演示：让表盘和应用自己轮流登场。',
     '<div class="demo-stat"><strong>138</strong><div><b>个演示场景</b><p>24 个应用 · 62 款表盘及子功能</p></div></div>'
     '<div class="demo-flow">' + shot('demo-watchface', '表盘') + '<span>→</span>' + shot('demo-weather', '应用') + '<span>→</span>' + shot('demo-maze', '子功能') + '</div>'
     '<div class="demo-control"><b>暂停 / 继续 <em>·</em> 左右切换 <em>·</em> 随时退出</b>'
     '<p>从「设置 → 演示模式」进入。</p></div>'
     '<p class="demo-limit">演示使用独立样例，画面持续标记 DEMO。<br>目前演示目录还不包含小咪与股票。</p>',
     '', '上方为原生界面与切换流程示意 · 演示模式尚未公开发布')

page('开源与进度', '07 / 继续折腾这个小圆屏', '桌面小搭子，<br>慢慢多一点本事。', 'ESP32-S3 上的开源小系统，继续慢慢打磨。',
     '<div class="recap"><div>' + shot('mimi-awake') + '<p><b>小咪</b><span>离线互动的小宠物</span></p></div>'
     '<div>' + shot('stocks-overview') + '<p><b>自选行情</b><span>关注的股票，放到手边看</span></p></div>'
     '<div><span class="play-icon">▶</span><p><b>自动演示</b><span>拍摄展示时，少一点手动操作</span></p></div></div>'
     '<div class="progress"><b>本帖三项功能均为本地开发版</b><p>尚未公开发布，继续调试与体验验证。</p></div>'
     '<div class="links"><span>已有正式版与项目说明</span><b>GitHub · soBigRice/soRound_os</b><p>个人主页：sobigrice.com</p></div>'
     '<p class="question">你会先打开小咪，还是自选行情？</p>'
     '<p class="credit">小咪形象 / 动作基于开源项目适配：GrokBot（BSD-3-Clause）· bloub（MIT）</p>', 'closing')

(SOURCE / 'pages.json').write_text(json.dumps(PAGES, ensure_ascii=False, indent=2) + '\n')
cards = ''.join(f'<a href="发布配图/{p["filename"]}.jpg"><img src="发布配图/{p["filename"]}.jpg" alt="{escape(p["title"])}"><span>{escape(p["filename"])}</span></a>' for p in PAGES)
(OUT / 'index.html').write_text('''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>soRound OS · 小红书图文</title><style>body{font:16px -apple-system,BlinkMacSystemFont,"PingFang SC",sans-serif;background:#f4f2eb;color:#17191c;max-width:1440px;margin:50px auto;padding:0 28px}h1{font-size:36px}p{line-height:1.7}nav{display:flex;gap:24px;margin:25px 0}a{color:inherit}section{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:24px}section a{text-decoration:none}img{display:block;width:100%;border-radius:12px}span{display:block;margin-top:10px}</style><h1>小咪、自选行情与自动演示</h1><p>8 张 1080 × 1440 图文；按编号上传 JPG。原生界面排版预览，本地新功能尚未公开发布。</p><nav><a href="发布文案.md">主帖文案</a><a href="拆分短帖.md">两篇拆分稿</a><a href="00-配图总览.jpg">配图总览</a></nav><section>''' + cards + '</section></html>')
print(f'已生成 {len(PAGES)} 页及 {len(manifest)} 份原图副本 / Created layouts and preserved assets.')
