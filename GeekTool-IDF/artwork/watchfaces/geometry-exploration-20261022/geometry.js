/* 几何概念稿的唯一绘制来源；不接设备。 / Single rendering source for geometry concepts; no device access. */
(() => {
  'use strict';
  const designs = [
    {key:'cross', number:'01', name:'坐标', en:'CROSS', group:'position', rule:'横坐标是24小时，纵坐标是分钟。'},
    {key:'balance', number:'02', name:'支点', en:'BALANCE', group:'position', rule:'长端朝向读小时，游标沿杆的位置读分钟。'},
    {key:'hourglass', number:'03', name:'沙漏', en:'HOURGLASS', group:'area', rule:'上方面积是12小时，下方面积是分钟。'},
    {key:'quanta', number:'04', name:'方量', en:'QUANTA', group:'area', rule:'大方块面积是24小时，小方块面积是分钟。'},
    {key:'phase', number:'05', name:'合相', en:'PHASE', group:'rotation', rule:'大半圆弧顶指小时，小半圆弧顶指分钟。'},
    {key:'aperture', number:'06', name:'开合', en:'APERTURE', group:'rotation', rule:'缝隙宽度是24小时，双片顺时针旋转读分钟。'}
  ];
  const palette = {paper:'#e9e6db', mint:'#9ab8ac', gold:'#d5a36c', dim:'#373e3c'};
  const n = value => Number(value.toFixed(4));
  const points = list => list.map(([x,y])=>`${n(x)},${n(y)}`).join(' ');
  const polygon = (list,fill,extra='') => `<polygon points="${points(list)}" fill="${fill}" ${extra}/>`;
  const line = (x1,y1,x2,y2,stroke,width=2,extra='') => `<line x1="${n(x1)}" y1="${n(y1)}" x2="${n(x2)}" y2="${n(y2)}" stroke="${stroke}" stroke-width="${width}" ${extra}/>`;
  const circle = (x,y,r,fill,extra='') => `<circle cx="${n(x)}" cy="${n(y)}" r="${n(r)}" fill="${fill}" ${extra}/>`;
  function time(hour,minute) {
    if(!Number.isInteger(hour)||!Number.isInteger(minute)||hour<0||hour>23||minute<0||minute>59) throw new RangeError('Time must be HH:MM within one day');
    return {hour,minute,day:(hour+minute/60)/24,h12:(hour%12+minute/60)/12,m:minute/60,stamp:`${String(hour).padStart(2,'0')}:${String(minute).padStart(2,'0')}`};
  }
  function corners(cx,cy,side) {
    const loX=cx-side/2,loY=cy-side/2,hiX=cx+side/2,hiY=cy+side/2;
    return [[loX,loY,1,1],[hiX,loY,-1,1],[hiX,hiY,-1,-1],[loX,hiY,1,-1]].map(([x,y,dx,dy])=>line(x,y,x+dx*11,y,palette.dim,1.5)+line(x,y,x,y+dy*11,palette.dim,1.5)).join('');
  }
  function cross(t) {
    const x=104+t.day*258,y=364-t.m*258;
    return corners(233,235,258)+line(x,106,x,364,palette.paper,13)+line(104,y,362,y,palette.mint,3)+circle(x,y,9,palette.mint,'stroke="#000" stroke-width="4"');
  }
  function balance(t) {
    const angle=t.h12*Math.PI*2,u=[Math.sin(angle),-Math.cos(angle)];
    const at=distance=>[233+u[0]*distance,224+u[1]*distance];
    const start=at(-90),end=at(145),cursor=at(-90+235*t.m);
    return line(...start,...end,palette.paper,13,'stroke-linecap="round"')+circle(233,224,17,'#000',`stroke="${palette.dim}" stroke-width="2"`)+circle(...end,4,palette.paper)+circle(...cursor,13,palette.gold,'stroke="#000" stroke-width="4"');
  }
  function hourglass(t) {
    const outline='stroke="#3b4140" stroke-width="1.5" stroke-linejoin="round"';
    // 面积随时间线性变化：三角形填充高度必须用平方根反推。
    // Area, not height, is linear in time; invert triangular area with a square root.
    const depth=p=>132*(1-Math.sqrt(1-p));
    function fill(p,top) {
      if(p===0) return '';
      const d=depth(p),inset=121*d/132,y=top?94:372,next=top?y+d:y-d;
      return polygon([[112,y],[354,y],[354-inset,next],[112+inset,next]],top?palette.paper:palette.mint,`data-area="${top?'hour':'minute'}"`);
    }
    return polygon([[112,94],[354,94],[233,226]],'none',outline)+polygon([[233,240],[112,372],[354,372]],'none',outline)+fill(t.h12,true)+fill(t.m,false);
  }
  function quanta(t) {
    // 正方形边长按sqrt(progress)缩放，保持可见面积与时分成正比。
    // Scale square side by sqrt(progress), keeping visible area proportional to time.
    const square=(cx,cy,max,p,color,kind)=>{
      const side=max*Math.sqrt(p);
      return p===0?'':`<rect x="${n(cx-side/2)}" y="${n(cy-side/2)}" width="${n(side)}" height="${n(side)}" fill="${color}" data-area="${kind}"/>`;
    };
    return corners(161,177,164)+corners(312,310,128)+square(161,177,164,t.day,palette.paper,'hour')+square(312,310,128,t.m,palette.mint,'minute');
  }
  function phase(t) {
    const semi=(r,color,angle)=>`<g transform="rotate(${n(angle)} 233 224)"><path d="M ${233-r} 224 A ${r} ${r} 0 0 1 ${233+r} 224 Z" fill="${color}" stroke="#000" stroke-width="7"/></g>`;
    // 半圆弧顶有方向，不是月相；两形共用十二点为角度起点。
    // Each semicircle has a directed bulge, not a lunar phase; both start at twelve o'clock.
    return circle(233,51,2,'#626d66')+semi(143,palette.paper,t.h12*360)+semi(96,palette.mint,t.m*360);
  }
  function aperture(t) {
    const gap=6+102*t.day,half=gap/2;
    // 两片异色区分180度方向，避免只显示30分钟周期。
    // Different slab colors disambiguate 180 degrees, preserving the full sixty-minute cycle.
    return `<g transform="rotate(${n(t.m*360)} 233 224)">`+polygon([[125,116],[233-half,116],[233-half,332],[125,332]],palette.paper)+polygon([[233+half,116],[341,116],[341,332],[233+half,332]],'#7f998d')+'</g>';
  }
  const drawers={cross,balance,hourglass,quanta,phase,aperture};
  function svg(key,hour=10,minute=8,assist=false) {
    const design=designs.find(item=>item.key===key);if(!design)throw new RangeError('Unknown geometry design');
    const t=time(hour,minute),hint=assist?`<text x="233" y="410" text-anchor="middle" fill="#b3b7b0" font-family="Arial,sans-serif" font-size="19" letter-spacing="4">${t.stamp}</text>`:'';
    return `<svg xmlns="http://www.w3.org/2000/svg" width="466" height="466" viewBox="0 0 466 466" role="img" aria-label="${design.name} ${t.stamp}；${design.rule}"><title>${design.name} ${t.stamp}</title><defs><clipPath id="screen-${key}"><circle cx="233" cy="233" r="233"/></clipPath></defs><g clip-path="url(#screen-${key})"><circle cx="233" cy="233" r="233" fill="#000"/>${drawers[key](t)}${hint}</g></svg>`;
  }
  globalThis.GeometryFaces={designs,time,svg};
})();
