(() => {
  'use strict';
  const C=233,TAU=Math.PI*2,pad=n=>String(n).padStart(2,'0'),hm=s=>`${pad(s.h)}:${pad(s.m)}`;
  const pt=(r,angle,cx=C,cy=C)=>[cx+Math.sin(angle)*r,cy-Math.cos(angle)*r];
  const txt=(x,y,text,size=16,color='#eae8dd',weight=400,extra='')=>`<text x="${x}" y="${y}" text-anchor="middle" font-size="${size}" font-weight="${weight}" fill="${color}" ${extra}>${text}</text>`;
  const line=(x1,y1,x2,y2,color,width=1,extra='')=>`<line x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}" stroke="${color}" stroke-width="${width}" ${extra}/>`;
  const circle=(x,y,r,color,extra='')=>`<circle cx="${x}" cy="${y}" r="${r}" fill="${color}" ${extra}/>`;
  const arc=(r,start,end,color,width=2)=>{const a=pt(r,start),b=pt(r,end);return `<path d="M${a} A${r},${r} 0 ${end-start>Math.PI?1:0} 1 ${b}" fill="none" stroke="${color}" stroke-width="${width}"/>`};
  const date=s=>`${pad(s.date.getMonth()+1)} / ${pad(s.date.getDate())} · TUE`;
  const ambientAod=(s,name)=>txt(C,174,name,13,'#4f554b',400,'letter-spacing="3"')+txt(C,265,hm(s),86,'#9a9d91',400)+line(183,294,283,294,'#373c33')+txt(C,323,'20 OCT',15,'#60685c');
  function maze(s,aod){
    if(aod)return ambientAod(s,'MAZE');
    let out=circle(C,C,233,'#e6e3d5');
    // 迷宫是装饰纹理，外圈小球才编码秒值。 / The maze is ornamental; the outer bead encodes seconds.
    [108,131,154,177].forEach((r,row)=>{for(let k=0;k<5;k++){const start=(k/5+row*.027)*TAU,end=start+TAU*.155;out+=arc(r,start,end,'#273229',3);if((k+row)%2===0){const a=pt(r,end),b=pt(r+23,end);out+=line(...a,...b,'#273229',3)}}});
    for(let i=0;i<60;i++){const a=pt(i%5?202:198,i*TAU/60),b=pt(207,i*TAU/60);out+=line(...a,...b,'#71776a',i%5?1:2)}
    out+=circle(C,C,88,'#e6e3d5')+txt(C,216,pad(s.h),94,'#26352d',600)+txt(C,299,pad(s.m),94,'#26352d',600)+line(195,235,271,235,'#c1c5b4',1.5);
    const bead=pt(189,s.s*TAU/60);out+=circle(...bead,8,'#d65c43')+circle(...bead,3,'#f5d6b3');
    return out+txt(C,76,'MAZE / 60',13,'#4d5a4b',600,'letter-spacing="3"')+txt(C,398,`${pad(s.s)} SECONDS`,12,'#4d5a4b',600,'letter-spacing="2"');
  }
  function flip(s,aod){
    if(aod)return ambientAod(s,'FOLD');
    let out=circle(C,C,233,'#293128')+circle(C,C,211,'none','stroke="#59624e" stroke-width="1"');
    out+=txt(C,66,'FOLD / TIME',14,'#b4bba4',600,'letter-spacing="4"');
    [92,239].forEach(y=>{out+=`<rect x="91" y="${y+4}" width="284" height="133" rx="14" fill="#111a12"/><rect x="96" y="${y}" width="274" height="133" rx="11" fill="#e7e3ce"/>`});
    out+=txt(C,211,pad(s.h),146,'#293128',600)+txt(C,358,pad(s.m),146,'#293128',600);
    [157,304].forEach(y=>{out+=line(97,y,369,y,'#566046',1.5)+line(97,y+2,369,y+2,'#fffae7',1);[94,362].forEach(x=>out+=`<rect x="${x}" y="${y-6}" width="10" height="13" rx="2" fill="#b6bda3" stroke="#4c5542" stroke-width="1"/>`)});
    return out+txt(C,408,'20 OCT   /   TUE',15,'#b4bba4',400,'letter-spacing="2"');
  }
  const digits={0:['01110','11011','11011','11011','11011','11011','01110'],1:['00110','01110','00110','00110','00110','00110','01111'],2:['01110','11011','00011','00110','01100','11000','11111'],3:['11110','00011','00011','01110','00011','00011','11110'],4:['00011','00111','01111','11011','11111','00011','00011'],5:['11111','11000','11000','11110','00011','00011','11110'],6:['01110','11000','11000','11110','11011','11011','01110'],7:['11111','00011','00110','00110','01100','01100','01100'],8:['01110','11011','11011','01110','11011','11011','01110'],9:['01110','11011','11011','01111','00011','00011','01110'],':':['00','00','11','00','11','00','00']};
  function pixelTime(s){let out='',x=107;for(const ch of hm(s)){digits[ch].forEach((row,y)=>{[...row].forEach((v,col)=>{if(v==='1')out+=`<rect x="${x+col*10}" y="${120+y*10}" width="8" height="8" fill="#d5e9ac"/>`})});x+=(ch===':'?3:6)*10}return out}
  function pixel(s,aod){
    if(aod)return ambientAod(s,'PIXEL CAMP');
    let out=circle(C,C,233,'#101b15')+txt(C,80,'20 OCT  /  TUE',15,'#9dae8b',400,'letter-spacing="3"')+pixelTime(s);
    [[103,228],[151,244],[338,227],[376,279],[240,228]].forEach(([x,y])=>out+=`<rect x="${x}" y="${y}" width="3" height="3" fill="#879f72"/>`);
    out+=circle(338,259,14,'#b8cba1')+circle(344,253,14,'#101b15');
    // 像素景物按秒换帧，不代表天气或养成数据。 / Pixel scenery changes each second; it is not weather or pet telemetry.
    out+=`<path d="M100 354 H366 M103 357 H168 M186 357 H266 M301 357 H361" stroke="#6c805c" stroke-width="3"/><path d="M177 350 L227 278 L281 350 Z" fill="#8eaa79"/><path d="M227 278 L245 350 L208 350 Z" fill="#314c36"/><path d="M227 278 L257 350" stroke="#b5c694" stroke-width="2"/>`;
    const cat=['10001','11111','11011','11111','01110','01110','11111'];
    cat.forEach((row,y)=>[...row].forEach((v,x)=>{if(v==='1')out+=`<rect x="${115+x*5}" y="${315+y*5}" width="5" height="5" fill="#c7d8aa"/>`}));
    out+=`<path d="M140 345 H150 V${s.s%2?325:335} H145" fill="none" stroke="#c7d8aa" stroke-width="5"/><path d="M291 352 l20 -3 M294 348 l19 4" stroke="#90795c" stroke-width="4"/>`;
    const flame=s.s%2?4:0;out+=`<path d="M297 345 v-14 h5 v-${12+flame} h5 v8 h5 v18 Z" fill="#db7f50"/><rect x="302" y="334" width="5" height="11" fill="#eecb7c"/>`;
    return out+txt(C,402,`SECOND ${pad(s.s)}`,13,'#9dae8b',400,'letter-spacing="3"');
  }
  function horizon(s,aod){
    if(aod)return ambientAod(s,'HORIZON');
    const day=s.h>=7&&s.h<19,progress=day?(s.h+s.m/60-7)/12:((s.h+s.m/60+5)%24)/12;
    const sx=85+296*Math.max(0,Math.min(1,progress)),sy=315-105*Math.sin(Math.PI*Math.max(0,Math.min(1,progress)));
    let out=circle(C,C,233,day?'#2b2824':'#171f30');
    out+=circle(sx,sy,91,day?'#d69565':'#b6bca8');
    if(!day)out+=circle(sx+39,sy-19,84,'#171f30');
    out+=`<path d="M0 309 Q115 300 233 309 T466 309 V466 H0Z" fill="${day?'#9f6044':'#384453'}"/><path d="M0 332 Q110 314 233 331 T466 325 V466 H0Z" fill="${day?'#754b3d':'#2d3945'}"/><path d="M0 357 Q129 333 258 359 T466 342 V466 H0Z" fill="${day?'#503e35':'#24303a'}"/><path d="M0 389 Q120 357 270 389 T466 378 V466 H0Z" fill="${day?'#342f28':'#1b252e'}"/>`;
    out+=txt(C,77,'HORIZON',14,'#cdc6af',400,'letter-spacing="4"')+txt(C,218,hm(s),96,'#f4ecda',400,'letter-spacing="-3"');
    out+=line(152,251,314,251,'#f4ecda',.8,'opacity=".55"')+txt(C,277,day?'DAYLIGHT HOURS':'AFTER DARK',12,'#f4ecda',400,'letter-spacing="3"');
    return out+txt(C,407,date(s),15,'#d8c7aa',400,'letter-spacing="2"');
  }
  function tide(s,aod){
    if(aod)return ambientAod(s,'TIDE');
    let out=circle(C,C,233,'#101e28')+txt(C,80,'TIDE',15,'#b7c7c8',400,'letter-spacing="5"')+txt(C,216,hm(s),99,'#e8e9db',400,'letter-spacing="-3"')+txt(C,252,'20 OCTOBER / TUE',13,'#9cbbbd',400,'letter-spacing="2"');
    for(let k=0;k<7;k++){
      const y=289+k*19,offset=Math.sin(s.s*TAU/60+k*.7)*9;
      out+=`<path d="M-30 ${y} C50 ${y-24+offset},105 ${y+24+offset},185 ${y} S320 ${y-24-offset},390 ${y} S465 ${y+23},500 ${y}" fill="none" stroke="${k===0?'#92b9ba':'#48787d'}" stroke-width="${k===0?2:1.5}" opacity="${1-k*.09}"/>`;
    }
    const bx=86+s.s/59*294,by=289+Math.sin((s.s/59)*TAU)*8;
    out+=circle(bx,by,5,'#eea577')+line(bx,by-16,bx,by-5,'#eea577',1);
    return out+txt(C,410,`${pad(s.s)} / 60`,15,'#b7c7c8',400,'letter-spacing="3"');
  }
  function growth(s,aod){
    if(aod)return ambientAod(s,'GROWTH');
    let out=circle(C,C,233,'#242820');
    for(let n=0;n<12;n++){
      const r=30+n*15,points=[];for(let i=0;i<=120;i++){const a=i/120*TAU,rr=r+4*Math.sin(a*3+n*.9)+3*Math.cos(a*7+n*.7);points.push(pt(rr,a,231,235).map(v=>v.toFixed(2)).join(','))}
      out+=`<path d="M${points.join('L')}Z" fill="none" stroke="${n%3===0?'#9a9d72':'#737f5c'}" stroke-width="${n%3===0?1.4:1}" opacity="${n%3===0?.72:.65}"/>`;
    }
    out+=txt(C,76,'GROWTH / 2026',13,'#d1cfb1',400,'letter-spacing="3"');
    [0,3,6,9].forEach(hour=>{const p=pt(192,hour/12*TAU);out+=circle(...p,3,'#d3ceb0')});
    const hour=pt(94,((s.h%12)+s.m/60)*TAU/12),minute=pt(151,(s.m+s.s/60)*TAU/60),second=pt(172,s.s*TAU/60);
    out+=line(C,C,...hour,'#e9e5c9',7,'stroke-linecap="round"')+line(C,C,...minute,'#e9e5c9',3.5,'stroke-linecap="round"')+line(C,C,...second,'#da9c63',1.5)+circle(C,C,6,'#d9d8be')+circle(C,C,2,'#242820');
    out+=`<rect x="171" y="298" width="124" height="42" rx="21" fill="#242820"/>`+txt(C,329,hm(s),30,'#dedbc0',400,'letter-spacing="2"');
    return out+txt(C,407,'20 OCT / TUE',14,'#b6b99a',400,'letter-spacing="2"');
  }
  window.naturePlayFaces={maze,flip,pixel,horizon,tide,growth};
})();
