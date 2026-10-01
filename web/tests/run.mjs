import { build } from 'esbuild';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { pathToFileURL, fileURLToPath } from 'node:url';

// 使用 Vite 已安装的 esbuild 转译测试;不引入新的测试依赖,产物只在临时目录。
const dir = await mkdtemp(join(tmpdir(), 'geektwin-test-'));
try {
  const outfile = join(dir, 'tests.mjs');
  await build({ entryPoints: [fileURLToPath(new URL('./performance.test.ts', import.meta.url))], outfile,
    bundle: true, platform: 'node', format: 'esm', logLevel: 'warning' });
  await import(pathToFileURL(outfile).href);
} finally {
  await rm(dir, { recursive: true, force: true });
}
