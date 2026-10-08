const SIZE=466, WHITE='#f3f1eb', GRAY='#8d8d94', DIM='#25252b', RED='#d1283a';
const clockIds=['mark','arc','numeral','orbit','frame','dots'];
const contexts=Object.fromEntries([...clockIds,'maze','fluid-overlay'].map(id=>[id,document.getElementById(id).getContext('2d')]));
const clocks=Object.fromEntries(clockIds.map(id=>[id,{aod:false,running:false}]));
const reducedMotion=matchMedia('(prefers-reduced-motion: reduce)');
const reviewParams=new URLSearchParams(location.search);
if(reviewParams.has('capture'))document.body.classList.add('capture');
if(reviewParams.get('section')==='clocks')document.body.classList.add('clocks-only');
if(reviewParams.get('section')==='fluid')document.body.classList.add('fluid-only');

function line(c,x1,y1,x2,y2,width,color,cap='round'){
  c.beginPath();c.moveTo(x1,y1);c.lineTo(x2,y2);c.strokeStyle=color;c.lineWidth=width;c.lineCap=cap;c.stroke();
}
function dot(c,x,y,r,color){c.fillStyle=color;c.beginPath();c.arc(x,y,r,0,Math.PI*2);c.fill();}
function arc(c,x,y,r,start,end,width,color){
  c.beginPath();c.arc(x,y,r,start,end);c.lineWidth=width;c.strokeStyle=color;c.lineCap='butt';c.stroke();
}
function text(c,value,x,y,size,color=WHITE,weight=400){
  c.font=`${weight} ${size}px Barlow,"PingFang SC",sans-serif`;c.textAlign='center';c.textBaseline='middle';c.fillStyle=color;c.fillText(value,x,y);
}
function point(angle,r){return [233+Math.sin(angle)*r,233-Math.cos(angle)*r];}
function begin(c){c.clearRect(0,0,SIZE,SIZE);c.fillStyle='#000';c.fillRect(0,0,SIZE,SIZE);}
function hand(c,angle,length,width,color,hollow=false){
  c.save();c.translate(233,233);c.rotate(angle);c.fillStyle=color;
  c.beginPath();c.moveTo(-width*.5,13);c.lineTo(-width*.5,-length+12);c.lineTo(0,-length);c.lineTo(width*.5,-length+12);c.lineTo(width*.5,13);c.closePath();c.fill();
  if(hollow)line(c,0,-22,0,-length+23,Math.max(2,width-7),'#000','butt');
  c.restore();
}
function battery(c,y,aod){
  if(aod)return;
  line(c,203,y-4,219,y-4,1,GRAY,'butt');line(c,203,y+4,219,y+4,1,GRAY,'butt');
  line(c,203,y-4,203,y+4,1,GRAY,'butt');line(c,219,y-4,219,y+4,1,GRAY,'butt');line(c,222,y-2,222,y+2,2,GRAY,'butt');line(c,206,y,215,y,4,GRAY,'butt');text(c,'74%',249,y,15,GRAY);
}
function flatHand(c,angle,length,width,color){
  c.save();c.translate(233,233);c.rotate(angle);c.fillStyle=color;c.fillRect(-width/2,-length,width,length+16);c.restore();
}
function leafHand(c,angle,length,width,color){
  c.save();c.translate(233,233);c.rotate(angle);c.fillStyle=color;c.beginPath();c.moveTo(0,14);
  c.quadraticCurveTo(-width,-length*.52,0,-length);c.quadraticCurveTo(width,-length*.52,0,14);c.fill();c.restore();
}
function dottedDate(c,color){
  const digits=[['01110','10001','10001','10001','10001','10001','01110'],['01110','10001','10001','01110','10001','10001','01110']];
  digits.forEach((rows,i)=>rows.forEach((row,y)=>[...row].forEach((value,x)=>{if(value==='1')dot(c,210+i*27+x*4.5,320+y*4.5,1.4,color);})));
}

function drawClock(id,seconds){
  const c=contexts[id], state=clocks[id], shade=state.aod?'#8d8d87':WHITE;
  begin(c);
  const second=state.aod?0:36+(state.running?seconds:0), minute=8+second/60, hour=10+minute/60;
  const ha=hour*Math.PI/6,ma=minute*Math.PI/30,sa=second*Math.PI/30;
  if(id==='mark'){
    for(let i=0;i<60;i++){
      const major=i%5===0, a=i*Math.PI/30, [x1,y1]=point(a,major?190:203), [x2,y2]=point(a,214);
      if(state.aod&&!major)continue;
      line(c,x1,y1,x2,y2,major?4:1.5,major?shade:'#73737b','butt');
    }
    for(const [value,x,y] of [['12',233,83],['3',382,235],['6',233,383],['9',84,235]])text(c,value,x,y,32,shade,600);
    if(!state.aod){text(c,'soRound',233,150,16,GRAY);text(c,'THU 08 OCT',233,308,17,GRAY);battery(c,337,false);}
    hand(c,ha,107,14,shade);hand(c,ma,158,8,shade);
    if(!state.aod){const [x,y]=point(sa,179),[bx,by]=point(sa,-31);line(c,bx,by,x,y,2,RED);const [cx,cy]=point(sa,-23);arc(c,cx,cy,6,0,Math.PI*2,2,RED);}
    dot(c,233,233,9,'#000');dot(c,233,233,5.5,state.aod?shade:RED);
  }else if(id==='arc'){
    for(let i=0;i<12;i++){
      const a=i*Math.PI/6-Math.PI/2;
      arc(c,233,233,181,a-.043,a+.043,17,state.aod?shade:'#deded8');
    }
    if(!state.aod){
      arc(c,233,233,212,-Math.PI/2,Math.PI*1.5,2,DIM);
      arc(c,233,233,212,-Math.PI/2,ma-Math.PI/2,3,WHITE);
      const [x,y]=point(sa,212);dot(c,x,y,4.5,RED);
      for(let i=0;i<60;i++)if(i%5) {const [x,y]=point(i*Math.PI/30,206);dot(c,x,y,1.1,'#56565e');}
      arc(c,233,233,40,0,Math.PI*2,1,'#1d1d21');
      text(c,'soRound',233,130,15,GRAY);text(c,'THU',233,300,15,GRAY);text(c,'08 OCT',233,325,22,WHITE,600);battery(c,354,false);
    }
    hand(c,ha,113,16,shade,true);hand(c,ma,171,5,shade);
    const [tipx,tipy]=point(ma,171);dot(c,tipx,tipy,3.5,state.aod?shade:RED);
    dot(c,233,233,8,'#000');arc(c,233,233,7,0,Math.PI*2,2,shade);dot(c,233,233,2,state.aod?shade:RED);
  }else if(id==='numeral'){
    for(let i=0;i<60;i++){
      if(state.aod||i%5===0)continue;
      const a=i*Math.PI/30,[x1,y1]=point(a,207),[x2,y2]=point(a,212);line(c,x1,y1,x2,y2,1.2,'#62626a','butt');
    }
    for(let i=1;i<=12;i++){const [x,y]=point(i*Math.PI/6,179);text(c,String(i),x,y,i%3===0?31:24,shade,i%3===0?600:400);}
    if(!state.aod){
      text(c,'THU 08 OCT',233,151,14,GRAY);
      for(let i=0;i<12;i++){const a=i*Math.PI/6;line(c,233+Math.sin(a)*26,321-Math.cos(a)*26,233+Math.sin(a)*30,321-Math.cos(a)*30,1,'#73737b','butt');}
      line(c,233,321,233+Math.sin(sa)*23,321-Math.cos(sa)*23,1.5,RED);dot(c,233,321,2,RED);text(c,'soRound',233,371,14,GRAY);
    }
    leafHand(c,ha,119,12,shade);leafHand(c,ma,167,6,shade);dot(c,233,233,5,shade);dot(c,233,233,2,'#000');
  }else if(id==='orbit'){
    arc(c,233,233,103,0,Math.PI*2,1,state.aod?'#353539':'#46464d');
    for(let i=0;i<60;i++){
      if(state.aod&&i%5)continue;
      const a=i*Math.PI/30,[x,y]=point(a,207);dot(c,x,y,i%5?1.1:3.2,i%5?'#6b6b73':shade);
    }
    if(!state.aod){text(c,'THU 08 OCT',233,83,15,GRAY);battery(c,372,false);const [x,y]=point(sa,218);dot(c,x,y,3.5,RED);}
    const [hx,hy]=point(ha,103);line(c,233,233,hx,hy,2,state.aod?shade:RED);dot(c,hx,hy,19,state.aod?shade:RED);dot(c,hx,hy,4,'#000');
    const [mx,my]=point(ma,187);line(c,233,233,mx,my,4,shade,'butt');dot(c,mx,my,6,shade);dot(c,mx,my,2,'#000');
    dot(c,233,233,9,'#000');dot(c,233,233,3,shade);
  }else if(id==='frame'){
    for(let i=0;i<60;i++){
      const a=i*Math.PI/30,s=Math.sin(a),t=-Math.cos(a),m=Math.max(Math.abs(s),Math.abs(t));
      if(state.aod&&i%15)continue;
      const outer=156/m,inner=(i%15===0?139:i%5===0?148:152)/m;
      line(c,233+s*inner,233+t*inner,233+s*outer,233+t*outer,i%15===0?14:i%5===0?4:1,i%15===0?shade:GRAY,'butt');
    }
    if(!state.aod){
      c.strokeStyle='#27272d';c.lineWidth=1;c.strokeRect(99,99,268,268);
      text(c,'soRound',233,152,15,GRAY);text(c,'THU 08',233,309,22,shade,600);text(c,'OCTOBER',233,336,12,GRAY);
      const [sx,sy]=point(sa,207);dot(c,sx,sy,2.5,RED);
    }
    flatHand(c,ha,101,18,shade);flatHand(c,ma,169,6,state.aod?shade:RED);c.fillStyle='#000';c.fillRect(225,225,16,16);c.fillStyle=shade;c.fillRect(230,230,6,6);
  }else if(id==='dots'){
    for(let i=0;i<60;i++){
      const a=i*Math.PI/30,[x,y]=point(a,i%5?216:192);
      if(i%5){if(!state.aod)dot(c,x,y,1.6,'#5a5a63');}
      else for(const offset of [[-4,-4],[4,-4],[-4,4],[4,4]])dot(c,x+offset[0],y+offset[1],2.8,shade);
    }
    if(!state.aod){dottedDate(c,shade);text(c,'OCT',233,366,14,GRAY);const [x,y]=point(sa,216);dot(c,x,y,4.5,RED);}
    const [hx,hy]=point(ha,102),[mx,my]=point(ma,169);line(c,233,233,hx,hy,16,shade);line(c,233,233,mx,my,6,state.aod?shade:RED);
    dot(c,233,233,8,'#000');dot(c,233,233,4,shade);
  }
}

const maze={index:4,mode:'play',solution:false,x:0,y:0,vx:0,vy:0,tx:0,ty:0,completed:new Set(),dirty:true};
const OX=95, OY=114, BOARD=276, BALL_R=8, WALL=5;
const mazeCanvas=document.getElementById('maze');
const levelButtons=document.getElementById('level-buttons');
function resetMaze(index=maze.index){
  maze.index=index;maze.mode='play';maze.solution=false;maze.vx=maze.vy=maze.tx=maze.ty=0;
  const cell=BOARD/levels[index].n;maze.x=OX+cell/2;maze.y=OY+cell/2;maze.dirty=true;
  document.getElementById('maze-status').textContent=`第 ${index+1} 关 · ${levels[index].name}`;
  document.getElementById('show-path').setAttribute('aria-pressed','false');syncMazeButtons();
}
function syncMazeButtons(){
  levelButtons.hidden=maze.mode!=='menu';document.getElementById('maze-retry').hidden=maze.mode!=='play';
  const next=document.getElementById('maze-next');next.hidden=maze.mode!=='win';next.textContent=maze.index===11?'返回关卡 →':'下一关 →';
  document.getElementById('show-path').disabled=maze.mode!=='play';maze.dirty=true;
}
for(const level of levels){
  const button=document.createElement('button');button.className='screen-button level-hit';
  const col=(level.id-1)%4,row=Math.floor((level.id-1)/4);
  button.style.left=`${(134+col*66-28)/466*100}%`;button.style.top=`${(177+row*93-28)/466*100}%`;
  button.textContent=String(level.id).padStart(2,'0');button.setAttribute('aria-label',`第 ${level.id} 关 ${level.name}`);
  button.addEventListener('click',()=>resetMaze(level.id-1));levelButtons.append(button);
}
function chrome(c,title){
  arc(c,233,233,225,0,Math.PI*2,1.5,'#212125');
  // 现有共享电量环与返回位置；74%只用于设计样例。
  arc(c,233,233,225,-Math.PI/2,-Math.PI/2+Math.PI*2*.74,5,'#bebfbc');
  text(c,title,233,56,17,WHITE);
}
function drawMaze(){
  if(!maze.dirty)return;maze.dirty=false;
  const c=contexts.maze,level=levels[maze.index];begin(c);chrome(c,'迷宫');
  if(maze.mode==='menu'){
    text(c,'十二关，慢慢走。',233,107,21,WHITE,600);
    for(let row=0;row<3;row++){
      text(c,['入门  /  4 × 4','转向  /  5 × 5','精密  /  6 × 6'][row],233,140+row*93,14,GRAY);
      for(let col=0;col<4;col++){
        const id=row*4+col+1,x=134+col*66,y=177+row*93,done=maze.completed.has(id);
        dot(c,x,y,26,'#141418');arc(c,x,y,26,0,Math.PI*2,1,id===maze.index+1?RED:'#45454b');text(c,String(id).padStart(2,'0'),x,y,21,id===maze.index+1?WHITE:GRAY,600);
        if(done)dot(c,x,y+19,2.5,RED);
      }
    }
    text(c,'固定地图 · 随时重玩',233,415,14,GRAY);return;
  }
  if(maze.mode==='win'){
    for(let i=0;i<12;i++){const [x,y]=[233+Math.sin(i*Math.PI/6)*71,225-Math.cos(i*Math.PI/6)*71];dot(c,x,y,3.5,i<maze.index+1?RED:DIM);}
    text(c,String(level.id).padStart(2,'0'),233,213,66,WHITE,600);text(c,'完成',233,265,20,GRAY);text(c,`${level.name}  /  ${level.chapter}`,233,115,17,GRAY);return;
  }
  text(c,`${String(level.id).padStart(2,'0')} / 12  ·  ${level.name}`,233,94,16,GRAY);
  const cell=BOARD/level.n;c.fillStyle='#0b0b0d';c.fillRect(OX,OY,BOARD,BOARD);
  if(maze.solution){
    c.beginPath();for(let i=0;i<level.path.length;i++){const p=level.path[i],x=OX+(p%level.n+.5)*cell,y=OY+(Math.floor(p/level.n)+.5)*cell;if(i)c.lineTo(x,y);else c.moveTo(x,y);}
    c.strokeStyle='#78202c';c.lineWidth=2;c.lineJoin='round';c.setLineDash([3,7]);c.stroke();c.setLineDash([]);
  }
  for(let r=0;r<level.n;r++)for(let col=0;col<level.n;col++){
    const wall=level.walls[r*level.n+col],x=OX+col*cell,y=OY+r*cell;
    if(wall&1)line(c,x,y,x+cell,y,WALL,'#5f6069');
    if(wall&8)line(c,x,y,x,y+cell,WALL,'#5f6069');
  }
  line(c,OX,OY+BOARD,OX+BOARD,OY+BOARD,WALL,'#5f6069');line(c,OX+BOARD,OY,OX+BOARD,OY+BOARD,WALL,'#5f6069');
  arc(c,OX+cell/2,OY+cell/2,13,0,Math.PI*2,1,'#3b3b42');
  const goal=OX+BOARD-cell/2,goaly=OY+BOARD-cell/2;
  arc(c,goal,goaly,13,0,Math.PI*2,2,RED);dot(c,goal,goaly,4,RED);
  dot(c,maze.x,maze.y,BALL_R,WHITE);dot(c,maze.x-2,maze.y-2,2.5,'#fff');
  text(c,`${level.chapter}  ·  倾斜设备，滚入红点`,233,417,14,GRAY);
}
function mazeWalls(){
  const level=levels[maze.index],cell=BOARD/level.n,walls=[];
  for(let r=0;r<level.n;r++)for(let col=0;col<level.n;col++){
    const wall=level.walls[r*level.n+col],x=OX+col*cell,y=OY+r*cell;
    if(wall&1)walls.push([x-WALL/2,y-WALL/2,cell+WALL,WALL]);
    if(wall&8)walls.push([x-WALL/2,y-WALL/2,WALL,cell+WALL]);
  }
  walls.push([OX-WALL/2,OY+BOARD-WALL/2,BOARD+WALL,WALL],[OX+BOARD-WALL/2,OY-WALL/2,WALL,BOARD+WALL]);return walls;
}
let collisionWalls=[];
function simulateMaze(dt){
  if(maze.mode!=='play')return;
  const tx=maze.tx+(keys.has('ArrowRight')?1:0)-(keys.has('ArrowLeft')?1:0);
  const ty=maze.ty+(keys.has('ArrowDown')?1:0)-(keys.has('ArrowUp')?1:0);
  if(!tx&&!ty&&Math.abs(maze.vx)+Math.abs(maze.vy)<.3)return;
  const oldx=maze.x,oldy=maze.y,substeps=Math.ceil(dt/.007),sdt=dt/substeps;
  collisionWalls=mazeWalls();
  for(let s=0;s<substeps;s++){
    maze.vx=(maze.vx+tx*800*sdt)*Math.pow(.13,sdt);maze.vy=(maze.vy+ty*800*sdt)*Math.pow(.13,sdt);
    const speed=Math.hypot(maze.vx,maze.vy);if(speed>200){maze.vx*=200/speed;maze.vy*=200/speed;}
    let nx=maze.x+maze.vx*sdt,ny=maze.y+maze.vy*sdt;
    for(const [x,y,w,h] of collisionWalls){
      const cx=Math.max(x,Math.min(x+w,nx)),cy=Math.max(y,Math.min(y+h,ny));
      const dx=nx-cx,dy=ny-cy,d=Math.hypot(dx,dy);
      if(d<BALL_R&&d>.0001){const ax=dx/d,ay=dy/d;nx=cx+ax*BALL_R;ny=cy+ay*BALL_R;const vn=maze.vx*ax+maze.vy*ay;if(vn<0){maze.vx-=vn*ax;maze.vy-=vn*ay;}}
    }
    maze.x=nx;maze.y=ny;
  }
  maze.dirty=Math.abs(oldx-maze.x)+Math.abs(oldy-maze.y)>.005;
  const cell=BOARD/levels[maze.index].n;
  if(Math.hypot(maze.x-(OX+BOARD-cell/2),maze.y-(OY+BOARD-cell/2))<12){
    maze.completed.add(maze.index+1);maze.mode='win';maze.tx=maze.ty=0;keys.clear();
    document.getElementById('maze-status').textContent=`第 ${maze.index+1} 关已完成`;syncMazeButtons();
  }
}

// 染料渲染器只在流体进入可视区时分配；表盘审阅无需建立GPU上下文。
const fluid={playing:!reducedMotion.matches,previous:null};
const fluidCanvas=document.getElementById('fluid');
let inkStudy=null,fluidInView=typeof IntersectionObserver==='undefined'&&!document.body.classList.contains('clocks-only');
function drawFluid(){
  const overlay=contexts['fluid-overlay'];overlay.clearRect(0,0,SIZE,SIZE);chrome(overlay,'流体');
  if(!fluidInView)return;
  if(!inkStudy){
    inkStudy=createInkStudy(fluidCanvas,document.getElementById('fluid-status'));
    inkStudy.setPalette(document.getElementById('fluid-palette').value);
  }
  inkStudy.render();
}
function stirInk(x,y){
  drawFluid();if(!inkStudy)return;
  const current={x:(x+1)/2,y:(1-y)/2,time:performance.now()};
  const previous=fluid.previous||current;
  const dt=Math.max(.008,(current.time-previous.time)/1000);
  const dx=Math.max(-180,Math.min(180,(current.x-previous.x)*116/dt));
  const dy=Math.max(-180,Math.min(180,(current.y-previous.y)*116/dt));
  const steps=Math.max(1,Math.ceil(Math.hypot(current.x-previous.x,current.y-previous.y)/.015));
  for(let i=1;i<=steps;i++)inkStudy.inject(previous.x+(current.x-previous.x)*i/steps,previous.y+(current.y-previous.y)*i/steps,dx/steps,dy/steps);
  fluid.previous=current;inkStudy.render();
}
if(typeof IntersectionObserver!=='undefined'){
  const observer=new IntersectionObserver(entries=>{
    fluidInView=entries[0].isIntersecting;
    if(fluidInView)drawFluid();
  });
  observer.observe(fluidCanvas);
  window.addEventListener('pagehide',()=>observer.disconnect());
  window.addEventListener('pageshow',()=>observer.observe(fluidCanvas));
}

for(const button of document.querySelectorAll('[data-aod]'))button.addEventListener('click',()=>{
  const state=clocks[button.dataset.aod];state.aod=!state.aod;button.setAttribute('aria-pressed',String(state.aod));drawClock(button.dataset.aod,clockElapsed);
});
for(const button of document.querySelectorAll('[data-clock]'))button.addEventListener('click',()=>{
  const state=clocks[button.dataset.clock];state.running=!state.running;button.setAttribute('aria-pressed',String(state.running));
});
function openLevels(){maze.mode='menu';keys.clear();maze.tx=maze.ty=maze.vx=maze.vy=0;syncMazeButtons();document.getElementById('maze-status').textContent='入门 · 转向 · 精密';}
document.getElementById('maze-back').addEventListener('click',()=>maze.mode==='menu'?resetMaze():openLevels());
document.getElementById('choose-level').addEventListener('click',openLevels);
document.getElementById('maze-retry').addEventListener('click',()=>resetMaze());
document.getElementById('maze-next').addEventListener('click',()=>maze.index===11?openLevels():resetMaze(maze.index+1));
document.getElementById('show-path').addEventListener('click',event=>{maze.solution=!maze.solution;event.currentTarget.setAttribute('aria-pressed',String(maze.solution));maze.dirty=true;});
function resetFluid(){
  fluid.previous=null;drawFluid();if(inkStudy)inkStudy.reset();
}
document.getElementById('fluid-back').addEventListener('click',resetFluid);document.getElementById('fluid-reset').addEventListener('click',resetFluid);
const motionButton=document.getElementById('fluid-motion');motionButton.setAttribute('aria-pressed',String(fluid.playing));
motionButton.addEventListener('click',()=>{fluid.playing=!fluid.playing;motionButton.setAttribute('aria-pressed',String(fluid.playing));});
document.getElementById('fluid-palette').addEventListener('change',event=>{drawFluid();if(inkStudy)inkStudy.setPalette(event.target.value);});

function dragGravity(canvas,onMove,onEnd){
  let active=false;
  const move=event=>{if(!active)return;const r=canvas.getBoundingClientRect();onMove((event.clientX-r.left)/r.width*2-1,(event.clientY-r.top)/r.height*2-1);};
  canvas.addEventListener('pointerdown',event=>{active=true;canvas.focus();canvas.setPointerCapture(event.pointerId);move(event);});
  canvas.addEventListener('pointermove',move);
  const end=()=>{if(!active)return;active=false;onEnd();};canvas.addEventListener('pointerup',end);canvas.addEventListener('pointercancel',end);canvas.addEventListener('lostpointercapture',end);
}
dragGravity(mazeCanvas,(x,y)=>{maze.tx=x;maze.ty=y;},()=>{maze.tx=maze.ty=0;});
dragGravity(fluidCanvas,stirInk,()=>{fluid.previous=null;if(inkStudy)inkStudy.nextColor();});
const keys=new Set();
mazeCanvas.addEventListener('keydown',event=>{if(event.key.startsWith('Arrow')){event.preventDefault();keys.add(event.key);}});
mazeCanvas.addEventListener('keyup',event=>keys.delete(event.key));mazeCanvas.addEventListener('blur',()=>keys.clear());
fluidCanvas.addEventListener('keydown',event=>{
  if(!['ArrowLeft','ArrowRight','ArrowDown'].includes(event.key))return;event.preventDefault();
  if(event.key==='ArrowDown'){resetFluid();return;}
  drawFluid();if(!inkStudy)return;
  const direction=event.key==='ArrowLeft'?-1:1;
  for(let i=0;i<20;i++){const a=i/20*Math.PI*2;inkStudy.inject(.5+Math.cos(a)*.19,.5+Math.sin(a)*.19,-Math.sin(a)*direction*28,Math.cos(a)*direction*28,false);}
  inkStudy.render();
});

let raf=0,last=0,start=0,clockElapsed=0,lastClock=-1;
function frame(now){
  const dt=last?Math.min((now-last)/1000,.05):.016;last=now;if(!start)start=now;clockElapsed=Math.floor((now-start)/1000);
  if(clockElapsed!==lastClock){for(const id of clockIds)if(clocks[id].running)drawClock(id,clockElapsed);lastClock=clockElapsed;}
  simulateMaze(dt);drawMaze();
  if(fluid.playing&&fluidInView&&inkStudy){inkStudy.step(dt);inkStudy.render();}
  raf=requestAnimationFrame(frame);
}
function suspend(){cancelAnimationFrame(raf);raf=0;last=0;keys.clear();maze.tx=maze.ty=0;fluid.previous=null;}
function resume(){if(!document.hidden&&!raf)raf=requestAnimationFrame(frame);}
document.addEventListener('visibilitychange',()=>document.hidden?suspend():resume());window.addEventListener('pagehide',()=>{suspend();if(inkStudy){inkStudy.destroy();inkStudy=null;}});window.addEventListener('pageshow',resume);
reducedMotion.addEventListener('change',event=>{if(event.matches){fluid.playing=false;motionButton.setAttribute('aria-pressed','false');}});
resetMaze();document.fonts.ready.then(()=>{for(const id of clockIds)drawClock(id,0);drawMaze();drawFluid();resume();});
