// 生成静态审阅帧，和交互页共用绘制函数。 / Export review frames from the same renderer as the interactive page.
import fs from 'node:fs';
import vm from 'node:vm';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const root=path.dirname(fileURLToPath(import.meta.url)),context=vm.createContext({});
vm.runInContext(fs.readFileSync(path.join(root,'geometry.js'),'utf8'),context);
const {designs,svg}=context.GeometryFaces;
fs.mkdirSync(path.join(root,'states'),{recursive:true});
for(const design of designs){
  fs.writeFileSync(path.join(root,`${design.key}.svg`),svg(design.key));
  for(const [h,m] of [[0,0],[6,30],[23,59]])fs.writeFileSync(path.join(root,'states',`${design.key}-${String(h).padStart(2,'0')}${String(m).padStart(2,'0')}.svg`),svg(design.key,h,m));
}
const page=fs.readFileSync(path.join(root,'index.html'),'utf8');
for(const direction of ['position','area','rotation'])fs.writeFileSync(path.join(root,`${direction}.html`),page.replace('data-direction="all"','data-direction="'+direction+'"'));
// 单文件预览可离线打开，测试结束后不需要常驻服务器。
// The standalone preview opens offline without leaving a local server running.
const standalone=page.replace('<link rel="stylesheet" href="preview.css">',`<style>${fs.readFileSync(path.join(root,'preview.css'),'utf8')}</style>`)
  .replace('<script src="geometry.js" defer></script><script src="preview.js" defer></script>','')
  .replace('</body>',`<script>${fs.readFileSync(path.join(root,'geometry.js'),'utf8')}</script><script>${fs.readFileSync(path.join(root,'preview.js'),'utf8')}</script></body>`);
fs.writeFileSync(path.join(root,'preview.html'),standalone.split('\n').map(line=>line.trimEnd()).join('\n'));
console.log('Generated six concepts, eighteen boundary states, three direction pages and an offline standalone preview.');
