# 浏览器依赖与来源

2026-10-22 从官方 npm CDN / 维护者仓库保存；本目录只供浏览器预览，不进入固件组件。

- `maplibre-gl.js`、`maplibre-gl.css`：`https://unpkg.com/maplibre-gl@5.12.0/dist/`，固定 5.12.0；许可证见 `MAPLIBRE-LICENSE.txt`。
- `icons/*.svg`：`https://unpkg.com/feather-icons@4.29.2/dist/icons/`；许可证见 `FEATHER-LICENSE.txt`。`triangle.svg` 仅将填充从 none 改为 currentColor，用于北标。
- `openfreemap-dark.original.json`：`https://tiles.openfreemap.org/styles/dark` 的原始样式快照；当前使用的样式为上级 `map-style.json`。
- 样式来源与许可：`https://github.com/hyperknot/openfreemap-styles`、`https://github.com/openmaptiles/dark-matter-gl-style`；原许可分别保存在 `OPENFREEMAP-STYLES-LICENSE.md`、`DARK-MATTER-LICENSE.md`。

本地只保存库、图标、样式和许可，不预取地图瓦片。瓦片、sprite 与字体按当前视图从 OpenFreeMap 请求，遵循浏览器正常 HTTP 缓存。没有私有 Key、账户或代理。
