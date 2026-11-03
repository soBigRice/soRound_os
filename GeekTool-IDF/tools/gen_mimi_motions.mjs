// 在构建时采样原始矢量引擎；设备不引入 JS 或逐帧位图。
// Sample the original vector engine at build time; no JS runtime or bitmap movie on device.
import {readFileSync, writeFileSync, readdirSync, mkdtempSync, rmSync} from 'node:fs';
import {join, dirname, resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath, pathToFileURL} from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const vendor = join(root, 'artwork/mimi/vendor/bloub');
const temporary = mkdtempSync(join(tmpdir(), 'mimi-vector-source-'));
const numbers = s => (s.match(/-?\d*\.?\d+(?:e[-+]?\d+)?/gi) || []).map(Number);
const point = (x, y) => [Math.round(x), Math.round(y)];
const opacity = x => Math.round(Math.max(0, Math.min(1, x)) * 255);
const format = x => Array.isArray(x) ? `{${x.map(format).join(',')}}` : String(x);

try {
    for (const file of readdirSync(vendor).filter(x => x.endsWith('.ts'))) {
        const source = readFileSync(join(vendor, file), 'utf8').replace(/from '(\.\/[^']+)'/g, "from '$1.ts'");
        writeFileSync(join(temporary, file), source);
    }
    const {BotEngine} = await import(pathToFileURL(join(temporary, 'engine.ts')));
    const {STATES, SEQUENCE} = await import(pathToFileURL(join(temporary, 'states.ts')));
    const {RINGS, SWOOSH, COMET_RIBBONS} = await import(pathToFileURL(join(temporary, 'decor.ts')));
    const frameRows = [], clips = [];
    function eyeRing(eye) {
        if (!eye) return Array.from({length:48}, () => [0,0]);
        const first = numbers(eye.d.match(/^M([^A]+)/)[1]);
        const radius = numbers(eye.d.match(/A([^L]+)/)[1])[0];
        const hw = -first[0], hh = radius - first[1];
        const [a,b,c,d,e,f] = numbers(eye.matrix);
        const centers = [[-hw+radius,-hh+radius], [hw-radius,-hh+radius], [hw-radius,hh-radius], [-hw+radius,hh-radius]];
        const points = [];
        for (let corner=0; corner<4; ++corner) for (let i=0; i<12; ++i) {
            const angle = Math.PI + corner*Math.PI/2 + i/11*Math.PI/2;
            const x = centers[corner][0] + radius*Math.cos(angle);
            const y = centers[corner][1] + radius*Math.sin(angle);
            points.push(point(a*x+c*y+e, b*x+d*y+f));
        }
        return points;
    }
    for (const id of SEQUENCE) {
        const def = STATES.find(x => x.id === id), first = frameRows.length;
        const engine = new BotEngine(1000, id);
        const count = Math.ceil(def.duration*20)+1;
        for (let i=0; i<count; ++i) {
            const t = Math.min(i/20, def.duration), rendered = engine.sample(t), pose = def.pose(t);
            const body = [...rendered.bodyPath.matchAll(/[MC]([^MCZ]+)/g)].map(m => numbers(m[1]).slice(-2));
            body.pop();
            if (body.length !== 64) throw new Error(`Unexpected body path: ${id}`);
            const dots = Array.from({length:5}, (_,j) => {
                const dot=rendered.dots[j]; return dot ? [Math.round(dot.x),Math.round(dot.y),Math.round(dot.r)] : [0,0,0];
            });
            const dotOpacity = Array.from({length:5}, (_,j) => opacity((rendered.dots[j]?.opacity??0)*(rendered.dots[j]?.depth??1)));
            const arcOpacity = Array.from({length:6}, (_,j) => opacity(pose.arcs[j]?.opacity??0));
            const n=rendered.notif, notch=rendered.notch;
            const arcKind = id==='play'?2:id==='orbit'?1:id==='comet'?3:0;
            const row=[body.map(p=>point(...p)),[eyeRing(rendered.eyes[0]),eyeRing(rendered.eyes[1])],dots,
                n?[Math.round(n.x),Math.round(n.y),Math.round(n.r),Math.round(notch.r)]:[0,0,0,0],
                dotOpacity,[opacity(rendered.eyes[0]?.alpha??0),opacity(rendered.eyes[1]?.alpha??0)],
                arcOpacity,opacity(rendered.bodyAlpha),arcKind,pose.dotsBehind?1:0];
            frameRows.push(format(row));
        }
        clips.push({id, first, count, duration:Math.round(def.duration*1000)});
    }
    const groups = [RINGS,SWOOSH,COMET_RIBBONS];
    const seeds=groups.map(group => `{${group.map(s => format([s.a,s.k,s.tilt,s.speed,s.phase,s.sweep,s.width,s.cx,s.cy,s.hue,s.hueSpan])).join(',')}}`);
    const output = `// Generated from jeremy-prt/bloub b4bb3c1, MIT; see artwork/mimi/vendor/bloub/LICENSE.\n`+
        `// 20Hz vector keyframes; runtime interpolation follows the visible clock.\n`+
        `static const mimi_keyframe_t motion_frames[]={\n${frameRows.join(',\n')}\n};\n`+
        `static const mimi_clip_t motion_clips[]={${clips.map(c=>format([c.first,c.count,c.duration])).join(',')}};\n`+
        `static const mimi_arc_seed_t motion_arcs[3][6]={${seeds.join(',')}};\n`;
    writeFileSync(join(root,'main/mimi_motions.inc'),output);
    writeFileSync(join(vendor,'sampling.json'),JSON.stringify({revision:'b4bb3c1b5f93c7b87a2e8d620f667c4093d97749',fps:20,unitsPerRadius:1000,bodyPoints:64,eyePoints:48,clips},null,2)+'\n');
    console.log(`Mimi: ${clips.length} motion clips, ${frameRows.length} vector keyframes`);
} finally {
    rmSync(temporary,{recursive:true,force:true});
}
