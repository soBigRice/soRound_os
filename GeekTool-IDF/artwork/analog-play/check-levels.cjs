// 检查设计地图可解、没有捷径、难度递进；不替代真机碰撞与手感验收。
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const context = vm.createContext({});
vm.runInContext(fs.readFileSync(path.join(__dirname, 'levels.js'), 'utf8'), context);
const {levels, shortestPath} = vm.runInContext('({levels, shortestPath})', context);
let previous = 0;
for (const level of levels) {
  const route = shortestPath(level);
  assert.equal(JSON.stringify(route), JSON.stringify(level.path));
  assert(route.length > previous); previous = route.length;
  assert.equal(new Set(level.path).size, level.path.length);
  const queue = [0], seen = new Set([0]); let edges = 0;
  for (let cursor=0; cursor<queue.length; cursor++) {
    const cell=queue[cursor], x=cell%level.n, y=Math.floor(cell/level.n);
    const neighbours=[y ? cell-level.n : -1, x<level.n-1 ? cell+1 : -1,
      y<level.n-1 ? cell+level.n : -1, x ? cell-1 : -1];
    neighbours.forEach((next, d) => {
      if (next<0) {assert(level.walls[cell] & (1<<d)); return;}
      assert.equal(Boolean(level.walls[cell] & (1<<d)), Boolean(level.walls[next] & (1<<((d+2)%4))));
      if (!(level.walls[cell] & (1<<d))) {
        edges++;
        if (!seen.has(next)) {seen.add(next); queue.push(next);}
      }
    });
  }
  assert.equal(seen.size, level.n*level.n);
  assert.equal(edges/2, level.n*level.n-1);
  console.log(`${level.id} ${level.name}: ${level.n}x${level.n}, ${route.length-1} steps, connected tree`);
}
assert.equal(new Set(levels.map(level=>level.walls.join(','))).size, 12);
console.log('12 levels: solvable, reciprocal walls, unique, increasing route length.');

// 仅替代DOM接口，运行原型真正的连续坐标/墙体碰撞代码；按格子中心引导，
// 不能通过直接设置球坐标绕过墙体或直接调用过关分支。
const element = () => ({style:{}, classList:{add(){}, contains(){return false;}}, addEventListener(){},
  setAttribute(){}, append(){}, getContext(){return null;}});
const elements = new Map();
const getElement = id => {
  if (!elements.has(id)) elements.set(id, element());
  return elements.get(id);
};
const simulation = vm.createContext({console, URLSearchParams, location:{search:''},
  matchMedia:()=>({matches:false, addEventListener(){}}),
  document:{body:element(), getElementById:getElement, createElement:element, querySelectorAll:()=>[],
    fonts:{ready:{then(){}}}, addEventListener(){}},
  window:{addEventListener(){}}, requestAnimationFrame:()=>1, cancelAnimationFrame(){}});
for (const file of ['levels.js', 'preview.js']) {
  vm.runInContext(fs.readFileSync(path.join(__dirname, file), 'utf8'), simulation);
}
const {maze, resetMaze, simulateMaze} = vm.runInContext('({maze, resetMaze, simulateMaze})', simulation);
for (let index=0; index<levels.length; index++) {
  resetMaze(index);
  const level=levels[index], cell=276/level.n;
  for (const position of level.path.slice(1)) {
    const x=95+(position%level.n+.5)*cell, y=114+(Math.floor(position/level.n)+.5)*cell;
    let ticks=0;
    while (Math.hypot(maze.x-x, maze.y-y)>1 && maze.mode==='play' && ticks++<600) {
      maze.tx=Math.max(-1, Math.min(1, (x-maze.x)*.05-maze.vx*.006));
      maze.ty=Math.max(-1, Math.min(1, (y-maze.y)*.05-maze.vy*.006));
      simulateMaze(1/60);
    }
    assert(ticks<600, `Level ${index+1}, cell ${position}: collision prevented reaching waypoint`);
  }
  assert.equal(maze.mode, 'win');
  assert(maze.completed.has(index+1));
}
console.log('12 levels: actual preview collision simulation reaches the goal.');
