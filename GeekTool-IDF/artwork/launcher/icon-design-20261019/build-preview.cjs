// 仅生成可审阅的设计稿，不修改固件。 / Generate review artwork without changing firmware.
const fs = require('node:fs');
const path = require('node:path');
const sharp = require('sharp');

const root = __dirname;
const INK = '#f5f5f2';
const RED = '#d1283a';
const apps = [
  ['wifi','Wi-Fi','Wi-Fi','#347de8'], ['scan','I²C 扫描','I²C Scan','#7d6fe3'],
  ['system','系统信息','System','#6f879e'], ['weather','天气','Weather','#f0a83e'],
  ['calendar','日历','Calendar','#e36869'], ['countdown','倒计时','Countdown','#d29b4f'],
  ['stopwatch','秒表','Stopwatch','#e07b45'], ['settings','设置','Settings','#8a91a3'],
  ['ota','系统更新','Update','#6388d5'], ['audio','音频','Audio','#c97bca'],
  ['level','水平仪','Level','#6aab8e'], ['maze','迷宫','Maze','#79a15d'],
  ['fluid','流体','Fluid','#4eadd0'], ['dice','骰子','Dice','#d77a86'],
  ['remote','遥控器','Remote','#8e8dd2'], ['twin','双生','Twin','#5d9dab'],
  ['answers','答案之书','Answers','#b79065'], ['zodiac','星座运势','Zodiac','#a381d6'],
  ['merit','木鱼','Merit','#b38464'], ['pixels','方块','Pixels','#d47151'],
];

function svg(body, size = 148) {
  return `<svg xmlns="http://www.w3.org/2000/svg" width="${size}" height="${size}" viewBox="0 0 ${size} ${size}">${body}</svg>`;
}
function source(id) {
  if (id === 'countdown') return svg(`<path fill="${INK}" d="M32 20h84v14c0 20-21 32-31 40 10 8 31 20 31 40v14H32v-14c0-20 21-32 31-40-10-8-31-20-31-40Z"/><path fill="black" d="M45 33h58v8H45zm0 74h58v8H45z"/><path fill="${RED}" d="m74 63 8 11-8 11-8-11Z"/>`);
  if (id === 'level') return svg(`<rect x="16" y="48" width="116" height="52" rx="21" fill="${INK}"/><rect x="29" y="61" width="90" height="26" rx="12" fill="black"/><path d="M50 61h8v26h-8zm40 0h8v26h-8z" fill="${INK}"/><circle cx="74" cy="74" r="9" fill="${RED}"/>`);
  if (id === 'maze') return svg(`<path fill="${INK}" d="M20 20h108v108H64v-14h50V34H34v66H20zm28 28h52v14H62v24h38v14H48zM20 114h28v14H20z"/><circle cx="40" cy="107" r="8" fill="${RED}"/>`);
  if (id === 'pixels') return svg(`<g fill="${INK}"><rect x="20" y="23" width="28" height="28" rx="3"/><rect x="60" y="23" width="28" height="28" rx="3"/><rect x="20" y="63" width="28" height="28" rx="3"/><rect x="60" y="63" width="28" height="28" rx="3"/><rect x="100" y="63" width="28" height="28" rx="3"/><rect x="60" y="103" width="28" height="28" rx="3"/><rect x="100" y="103" width="28" height="28" rx="3"/></g><rect x="106" y="16" width="28" height="28" rx="3" fill="${RED}"/>`);
  if (id === 'twin') {
    const cube = `<path fill="${INK}" d="m38 16 30 17v35L38 85 8 68V33Z"/><path fill="none" stroke="black" stroke-width="5" stroke-linejoin="round" d="m8 33 30 17 30-17M38 50v35"/>`;
    return svg(`${cube}<g transform="translate(65 35)">${cube}</g><path fill="${RED}" d="M51 95h27v9H51z"/>`);
  }
  let raw = fs.readFileSync(path.join(root, 'sources', `${id}.svg`), 'utf8');
  raw = raw.replace(/currentColor/g, INK).replace(/<title>[\s\S]*?<\/title>/g, '');
  return raw.replace(/<svg\b([^>]*)>/, (tag, attributes) =>
    /\sfill=/.test(attributes) ? tag : `<svg fill="${INK}"${attributes}>`);
}

async function normalized(raw, span = 116) {
  const body = raw.match(/<svg[^>]*>([\s\S]*)<\/svg>/)[1];
  const view = raw.match(/viewBox="([^"]+)"/)[1].split(/\s+/).map(Number);
  const scan = await sharp(Buffer.from(raw)).resize(256,256,{fit:'fill'}).ensureAlpha().raw().toBuffer();
  let minX = 256, minY = 256, maxX = -1, maxY = -1, mass = 0, sx = 0, sy = 0;
  for (let y=0;y<256;y++) for (let x=0;x<256;x++) {
    const p = (y*256+x)*4;
    // 黑色负形不计为可见墨色。 / Exclude black cutouts from visible ink measurements.
    const weight = scan[p+3]/255 * Math.max(scan[p],scan[p+1],scan[p+2])/255;
    if (weight < .13) continue;
    minX=Math.min(minX,x); minY=Math.min(minY,y); maxX=Math.max(maxX,x); maxY=Math.max(maxY,y);
    mass+=weight; sx+=(x+.5)*weight; sy+=(y+.5)*weight;
  }
  if (!mass) throw new Error('Empty icon source');
  const width=maxX-minX+1, height=maxY-minY+1;
  const scale=span/Math.max(width,height);
  const middleX=(minX+maxX+1)/2, middleY=(minY+maxY+1)/2;
  // 限制光学校准，避免非对称符号被过度平移。 / Bound optical correction for asymmetric symbols.
  const correctionX=Math.max(-3.5,Math.min(3.5,(sx/mass-middleX)*scale*.42));
  const correctionY=Math.max(-3.5,Math.min(3.5,(sy/mass-middleY)*scale*.42));
  const tx=74-middleX*scale-correctionX, ty=74-middleY*scale-correctionY;
  return `<g transform="translate(${tx.toFixed(3)} ${ty.toFixed(3)}) scale(${(scale*256/view[2]).toFixed(6)} ${(scale*256/view[3]).toFixed(6)})" fill="${INK}">${body}</g>`;
}

async function pixelIcon(raw, id) {
  if (id === 'audio') {
    const heights=[32,64,88,112,80,56,24];
    return svg(heights.map((h,i)=>`<rect x="${18+i*16}" y="${74-h/2}" width="12" height="${h}" fill="${i===3?RED:INK}"/>`).join(''));
  }
  if (id === 'dice') return svg(`<rect x="18" y="18" width="112" height="112" fill="${INK}"/><g fill="black"><path d="M38 38h16v16H38zm56 0h16v16H94zm-56 56h16v16H38zm56 0h16v16H94z"/></g><rect x="66" y="66" width="16" height="16" fill="${RED}"/>`);
  const pixels = await sharp(Buffer.from(raw)).resize(16,16).ensureAlpha().raw().toBuffer();
  let body='';
  for (let y=0;y<16;y++) for (let x=0;x<16;x++) {
    const p=(y*16+x)*4;
    if (pixels[p+3]/255 * Math.max(pixels[p],pixels[p+1],pixels[p+2])/255 < .37) continue;
    const red=pixels[p] > pixels[p+1]*1.5 && pixels[p] > pixels[p+2]*1.25;
    body+=`<rect x="${10+x*8}" y="${10+y*8}" width="8" height="8" fill="${red?RED:INK}"/>`;
  }
  return svg(body);
}

function dataUrl(value) { return 'data:image/svg+xml;base64,'+Buffer.from(value).toString('base64'); }
const directions = [
  {id:'a-solid',tag:'A',title:'黑白实心',en:'SOLID / QUIET',description:'轮廓更完整，黑白更直接。少量红色只留给有含义的部位。',detail:'实心符号 · 宽负形 · 光学居中',recommend:true},
  {id:'b-color',tag:'B',title:'柔和彩色',en:'COLOR / SOFT',description:'用有体积的色块承托符号，让每个应用更容易区分。',detail:'圆角色块 · 应用分色 · 柔和层次'},
  {id:'c-pixel',tag:'C',title:'像素块面',en:'PIXEL / PLAY',description:'连续的像素块组成完整剪影，保留掌上设备的玩具感。',detail:'连续像素 · 大块剪影 · 红白节奏'},
];

const css = `
*{box-sizing:border-box}body{margin:0;background:#101112;color:#f5f5f2;font-family:-apple-system,BlinkMacSystemFont,"PingFang SC","Noto Sans SC",sans-serif}button,a{-webkit-tap-highlight-color:transparent}button{font:inherit}a{color:inherit;text-decoration:none}button:focus-visible,a:focus-visible{outline:2px solid #efb36f;outline-offset:5px}.page{max-width:1440px;margin:0 auto;padding:48px 56px 30px}.header{display:flex;justify-content:space-between;align-items:flex-start;margin-bottom:30px}.eyebrow{font-size:12px;letter-spacing:3px;color:#a3a5a7}.header h1{margin:12px 0 10px;font-size:42px;font-weight:580;letter-spacing:1px}.lead{font-size:15px;color:#b7b8b9;line-height:1.8;margin:0}.chip{font-size:12px;padding:7px 11px;border:1px solid #45413b;border-radius:20px;color:#e3be8a}.content{display:grid;grid-template-columns:466px minmax(0,1fr);gap:60px;align-items:start}.device{width:466px;height:466px;background:#000;border-radius:50%;position:relative;overflow:hidden;flex-shrink:0}.ring{position:absolute;inset:4px;border-radius:50%;background:conic-gradient(from 0deg,#efefef 0deg 266.4deg,#16161a 266.4deg 360deg);mask:radial-gradient(farthest-side,transparent calc(100% - 8px),#000 calc(100% - 7px));-webkit-mask:radial-gradient(farthest-side,transparent calc(100% - 8px),#000 calc(100% - 7px))}.icon-button{position:absolute;width:196px;height:196px;left:135px;top:119px;border:2px solid white;border-radius:50%;display:flex;align-items:center;justify-content:center;background:transparent}.hero-icon{width:148px;height:148px;display:block}.app-name{position:absolute;left:83px;top:329px;width:300px;text-align:center;font-size:24px;line-height:32px;font-weight:450;margin:0}.arrow{position:absolute;top:219px;width:28px;height:28px;border:0;padding:0;background:none;color:#969699;font-size:30px;line-height:28px;cursor:pointer}.arrow.prev{left:14px}.arrow.next{right:14px}.device-note{font-size:12px;color:#898b8e;margin-top:16px;line-height:1.7}.gallery-title{display:flex;justify-content:space-between;align-items:center;font-size:13px;color:#aaa;margin:0 0 18px}.grid{display:grid;grid-template-columns:repeat(5,minmax(0,1fr));column-gap:10px;row-gap:17px}.icon-card{background:none;color:inherit;border:1px solid transparent;border-radius:16px;padding:5px 0 8px;cursor:pointer;display:flex;flex-direction:column;align-items:center;gap:4px;min-width:0}.icon-card:hover{background:#1c1d1f}.icon-card[aria-pressed=true]{border-color:#4b4d51;background:#191a1c}.icon-card img{width:100px;height:100px;display:block}.icon-card span{font-size:12px;line-height:20px;color:#c3c4c5;white-space:nowrap}.controls{display:flex;align-items:center;gap:20px;margin:22px 0 0;font-size:13px;color:#b4b5b7}.controls label{cursor:pointer}.controls input{accent-color:#d1283a}.footer{margin-top:32px;padding-top:20px;border-top:1px solid #303134;display:flex;justify-content:space-between;color:#939598;font-size:12px;line-height:1.8}.guide .icon-button:before{content:"";position:absolute;inset:22px;border:1px dashed #4d5968;pointer-events:none}.guide .device:before,.guide .device:after{content:"";position:absolute;pointer-events:none;background:#385066;z-index:3}.guide .device:before{height:1px;width:196px;left:135px;top:217px}.guide .device:after{width:1px;height:196px;left:233px;top:119px}.home{max-width:1600px;margin:auto;padding:42px 40px 30px}.home-head{display:flex;align-items:flex-end;justify-content:space-between;margin-bottom:32px}.home h1{font-size:35px;font-weight:560;letter-spacing:1px;margin:12px 0}.home-head p{font-size:14px;color:#aaa;margin:0}.columns{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:34px}.direction{min-width:0}.direction h2{font-size:23px;font-weight:550;margin:0 0 8px}.direction .desc{font-size:13px;line-height:1.7;color:#b0b2b4;min-height:44px;margin:0 0 19px}.device-wrap{height:302px;display:flex;justify-content:center}.device-mini{width:300px;height:300px;position:relative}.device-mini .device{transform:scale(.6437768);transform-origin:top left}.direction .grid{grid-template-columns:repeat(4,minmax(0,1fr));gap:9px 2px;margin-top:21px}.direction .icon-card{cursor:default;gap:0;padding:0}.direction .icon-card img{width:80px;height:80px}.direction .icon-card span{font-size:12px}.direction .explore{font-size:12px;display:block;color:#c5b18f;margin-top:19px}.fineprint{color:#8b8d90;font-size:12px;line-height:1.8;border-top:1px solid #303134;margin-top:26px;padding-top:17px}.label-num{font-size:15px;color:#a3a4a6;margin-right:10px}.key{color:#d2b280;font-size:12px;margin-left:9px}.nav{display:flex;gap:20px;font-size:13px;color:#a7a9ac}.nav a[aria-current]{color:#fff}.legend{font-size:12px;color:#888;margin-top:12px}@media(max-width:1100px){.page{padding:28px}.content{grid-template-columns:1fr}.content>div:first-child{display:flex;flex-direction:column;align-items:center}.grid{grid-template-columns:repeat(5,minmax(0,1fr))}.columns{grid-template-columns:1fr}.direction{max-width:520px;margin:0 auto}.home-head{display:block}.home-head p{line-height:1.8}.footer{gap:20px}.header{gap:20px}.nav{flex-wrap:wrap}}@media(max-width:520px){.page,.home{padding:22px 16px}.header h1{font-size:30px}.content{gap:25px}.content>div:first-child{overflow:hidden}.device{transform:scale(.76);transform-origin:top center;margin-bottom:-112px}.device-mini .device{transform:scale(.6437768);transform-origin:top left;margin:0}.grid{grid-template-columns:repeat(4,minmax(0,1fr))}.icon-card img{width:76px;height:76px}.footer{display:block}.chip{display:none}.controls{font-size:12px;gap:12px}}`;

function device(icon, name, interactive = false) {
  return `<div class="device"><div class="ring"></div><div class="icon-button"><img class="hero-icon" src="${icon}" alt="${name}图标"></div><p class="app-name">${name}</p>${interactive?'<button class="arrow prev" aria-label="上一个应用">‹</button><button class="arrow next" aria-label="下一个应用">›</button>':'<span class="arrow prev">‹</span><span class="arrow next">›</span>'}</div>`;
}
function cards(icons, interactive) {
  return apps.map((app,i)=>`<${interactive?'button':'div'} class="icon-card" ${interactive?`data-index="${i}" aria-pressed="${i===0}"`:''}><img src="${icons[i]}" alt=""/><span>${app[1]}</span></${interactive?'button':'div'}>`).join('');
}
function head(title) { return `<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta http-equiv="Content-Security-Policy" content="default-src 'none';img-src data:;style-src 'unsafe-inline';script-src 'unsafe-inline'"><title>${title} · soRound OS</title><style>${css}</style></head>`; }

(async()=>{
  const all = [[],[],[]];
  for (const [id,zh,en,color] of apps) {
    const raw=source(id);
    const normalizedBody=await normalized(raw);
    const solid=svg(normalizedBody);
    all[0].push(dataUrl(solid));
    const glyph=(await normalized(raw,78)).replaceAll(INK,'#ffffff').replaceAll(RED,'#ffffff');
    const colored=svg(`<defs><linearGradient id="tile" x1="0" y1="0" x2=".8" y2="1"><stop stop-color="${color}"/><stop offset="1" stop-color="${color}" stop-opacity=".7"/></linearGradient><linearGradient id="shine" x2="0" y2="1"><stop stop-color="white" stop-opacity=".25"/><stop offset="1" stop-color="white" stop-opacity="0"/></linearGradient></defs><rect x="14" y="16" width="120" height="120" rx="35" fill="#000"/><rect x="14" y="12" width="120" height="120" rx="35" fill="url(#tile)"/><rect x="15" y="13" width="118" height="118" rx="34" fill="url(#shine)"/><g transform="translate(0 -2)">${glyph}</g>`);
    all[1].push(dataUrl(colored));
    all[2].push(dataUrl(await pixelIcon(solid,id)));
  }
  for (let n=0;n<directions.length;n++) {
    const d=directions[n], icons=all[n];
    const script=`const apps=${JSON.stringify(apps)},icons=${JSON.stringify(icons)};let index=0,en=false;function show(i){index=(i+apps.length)%apps.length;const name=apps[index][en?2:1];document.querySelector('.hero-icon').src=icons[index];document.querySelector('.hero-icon').alt=name+'图标';document.querySelector('.app-name').textContent=name;document.querySelectorAll('.icon-card').forEach((el,n)=>el.setAttribute('aria-pressed',n===index));}document.querySelectorAll('[data-index]').forEach(el=>el.onclick=()=>show(+el.dataset.index));document.querySelector('.prev').onclick=()=>show(index-1);document.querySelector('.next').onclick=()=>show(index+1);document.getElementById('guides').onchange=e=>document.body.classList.toggle('guide',e.target.checked);document.getElementById('english').onchange=e=>{en=e.target.checked;show(index)};document.addEventListener('keydown',e=>{if(e.key==='ArrowLeft')show(index-1);if(e.key==='ArrowRight')show(index+1)});`;
    const nav=directions.map(dir=>`<a href="${dir.id}.html" ${dir.id===d.id?'aria-current="page"':''}>${dir.tag} ${dir.title}</a>`).join('');
    const html=head(d.title)+`<body><main class="page"><header class="header"><div><div class="eyebrow">SOROUND OS / ICON STUDY ${d.tag}</div><h1>${d.title}</h1><p class="lead">${d.description}</p></div><span class="chip">${d.recommend?'推荐方向 · ':''}设计初稿</span></header><div class="content"><div>${device(icons[0],apps[0][1],true)}<p class="device-note">466 × 466 圆屏 · 图标画布 148 × 148<br>按左右键或点击右侧图标，检查完整 20 项。</p><div class="controls"><label><input type="checkbox" id="guides"> 中心 / 安全区</label><label><input type="checkbox" id="english"> English</label></div></div><div><div class="gallery-title"><span>20 个应用 / 同一套视觉规则</span><span>${d.detail}</span></div><div class="grid">${cards(icons,true)}</div></div></div><footer class="footer"><div>浏览器设计预览 · 74%电量为夹具<br>未经固件及设备验证</div><nav class="nav"><a href="index.html">三套对比</a>${nav}</nav></footer></main><script>${script}</script></body></html>`;
    fs.writeFileSync(path.join(root,d.id+'.html'),html);
  }
  const overview=head('图标设计方向')+`<body><main class="home"><header class="home-head"><div><div class="eyebrow">SOROUND OS / LAUNCHER ICONS / 2026.10.19</div><h1>先看形体，再选性格。</h1></div><p>三套完整图标 · 同一圆屏布局 · 每套 20 个应用</p></header><div class="columns">${directions.map((d,i)=>`<section class="direction"><h2><span class="label-num">${d.tag}</span>${d.title}${d.recommend?'<span class="key">推荐</span>':''}</h2><p class="desc">${d.description}</p><div class="device-wrap"><div class="device-mini">${device(all[i][7],apps[7][1])}</div></div><div class="grid">${cards(all[i],false)}</div><a class="explore" href="${d.id}.html">打开圆屏交互预览 ↗</a></section>`).join('')}</div><p class="fineprint">三套均为浏览器设计初稿，尚未接入固件。圆屏 466 × 466，图标画布 148 × 148，74% 电量为设计夹具。<br>通用符号基于 Phosphor Icons 2.1.1（MIT）；木鱼沿用已确认的轮廓；图标合辑只用于比较，设备仍为单应用居中显示。</p></main></body></html>`;
  fs.writeFileSync(path.join(root,'index.html'),overview);
  console.log('Generated 3 directions × 20 icons, 3 interactive pages and comparison page.');
})().catch(error=>{console.error(error);process.exitCode=1});
