// 固定主路线决定教学目标；支路只连接尚未加入的格子，不产生捷径。
// 所有布局由固定顺序构建，不使用随机数；相同关卡始终得到相同墙体。
const authored = [
  ['初见', 4, [0,1,2,3,7,11,15]],
  ['折返', 4, [0,4,8,9,10,6,7,11,15]],
  ['回廊', 4, [0,1,5,4,8,12,13,9,10,11,15]],
  ['四折', 4, [0,4,5,1,2,3,7,6,10,9,13,14,15]],
  ['蛇形', 5, [0,1,2,3,4,9,8,7,6,11,16,17,18,19,24]],
  ['双折线', 5, [0,5,10,11,6,7,2,3,4,9,14,13,12,17,22,23,24]],
  ['交错', 5, [0,1,6,5,10,15,16,11,12,7,8,3,4,9,14,19,18,23,24]],
  ['回旋', 5, [0,5,10,15,20,21,16,11,6,7,2,3,8,13,12,17,22,23,18,19,24]],
  ['长廊', 6, [0,1,2,3,4,5,11,17,16,15,14,13,7,6,12,18,24,30,31,32,33,34,35]],
  ['窄门', 6, [0,6,12,18,24,30,31,25,19,13,7,1,2,8,14,20,26,32,33,34,28,22,23,29,35]],
  ['错层', 6, [0,1,7,6,12,18,19,13,14,8,2,3,4,10,9,15,21,20,26,25,31,32,33,27,28,34,35]],
  ['归心', 6, [0,6,12,18,24,30,31,25,19,13,7,1,2,3,9,15,14,20,26,32,33,27,21,22,16,17,23,29,35]],
];

function neighbours(cell, n) {
  const x = cell % n, y = Math.floor(cell / n);
  return [y ? cell-n : -1, x<n-1 ? cell+1 : -1,
    y<n-1 ? cell+n : -1, x ? cell-1 : -1];
}

const levels = authored.map(([name, n, path], index) => {
  const walls = Array(n*n).fill(15);
  const visited = new Set(path);
  function connect(a, b) {
    const direction = neighbours(a, n).indexOf(b);
    if (direction < 0) throw new Error(`关卡 ${index+1} 存在不相邻路线`);
    walls[a] &= ~(1<<direction);
    walls[b] &= ~(1<<((direction+2)%4));
  }
  for (let i=1; i<path.length; i++) connect(path[i-1], path[i]);
  while (visited.size < n*n) {
    const before = visited.size;
    for (let cell=0; cell<n*n; cell++) {
      if (visited.has(cell)) continue;
      const options = neighbours(cell, n);
      for (let d=0; d<4; d++) {
        const next = options[(d+index)%4];
        if (next >= 0 && visited.has(next)) {
          connect(cell, next); visited.add(cell); break;
        }
      }
    }
    if (visited.size === before) throw new Error('固定关卡支路未连接');
  }
  return { id:index+1, name, n, path, walls,
    chapter:['入门','转向','精密'][Math.floor(index/4)] };
});

function shortestPath(level) {
  const queue = [[0]], seen = new Set([0]);
  for (let i=0; i<queue.length; i++) {
    const path = queue[i], cell = path.at(-1);
    if (cell === level.n*level.n-1) return path;
    neighbours(cell, level.n).forEach((next, d) => {
      if (next<0 || (level.walls[cell]&(1<<d)) || seen.has(next)) return;
      seen.add(next); queue.push([...path,next]);
    });
  }
  return [];
}
