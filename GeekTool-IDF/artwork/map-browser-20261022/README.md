# Orbit：圆屏地图交互稿

核对日期：2026-10-22。用户已选择第二张 `concepts/orbit.png` 并要求做成可操作 UI；本目录为该方向的浏览器实现，视觉效果仍待用户验收。未修改固件、设置、启动器或分区，未烧录、提交、推送或发布。

## 运行与审阅

在本目录运行：

```sh
python3 -B -m http.server 8765 --bind 127.0.0.1
```

打开 `http://127.0.0.1:8765/`。SDK、图标与样式保存在本地，无需安装 npm 依赖；真实地图瓦片及字体仍需互联网。交付时仅保留用户操作所需的这一个本机预览服务，测试结束后可关闭服务。页面退出会销毁地图和 worker。

- `orbit-native.png`：当前 466×466 的真实浏览器截图。
- `orbit-mobile.png`：390px 视口截图；圆屏等比缩放，无横向溢出。
- `orbit-picker.png`：地点选择层截图。
- `orbit-comparison.png`：已选概念与当前原尺寸画面的同图对照；验证结论见 `design-qa.md`。
- `concepts/` 为三个视觉探索原件；仅 `orbit.png` 已被选定。
- `map-fullscreen.png` 为用户已否定的旧 UI，仅留作生成概念时的原始输入，不代表当前实现。

## 交互与代码入口

`index.html` 定义固定 466×466 的圆形显示区；`preview.css` 负责等比适配及沿弧排布；`preview.js` 负责 MapLibre 生命周期与交互；`map-style.json` 为实际使用的地图样式。

| 操作 | 实现与边界 |
| --- | --- |
| 拖动地图 | MapLibre `dragPan`；惯性结束后 `moveend → updateView` 更新地图的可访问中心描述。北朝上，禁用旋转与倾斜。 |
| 右弧拖动缩放 | `pointerdown → setPointerCapture → zoomAtPointer → progressToZoom → setZoom`；使用显示后的圆屏矩形换算 466px 逻辑坐标，圆弧半径 212、角度 ±26°、命中宽度 44px。指针释放、取消或失去捕获后清空状态，不向地图传播拖动。 |
| 缩放范围与键盘 | 保留 4–18 级；默认比例映射到滑轨中点，上下半轨分别映射默认级别到最大/最小。`zoomToProgress` 与 `progressToZoom` 互逆；± / 方向键每次 0.5 级，PageUp/Down 每次 1 级，Home/End 到边界。边界禁用对应按钮。 |
| 回到起点 | `home → stop → jumpTo` 恢复所选地点的中心与默认缩放。上海默认为 WGS84 `[121.4892, 31.239]`、14.2 级；这是浏览起点，非设备定位。 |
| 选择地点 | 点击弧形地名或返回键 → `picker`；上海、杭州、北京、成都为四个样例起点。选择成功即回地图，取消保留视野。选择层隐藏底图与地图工具，避免可见地图的署名被遮挡。支持 Esc 和 Tab 焦点循环。 |
| 加载与失败 | `dataloading` 延迟显示加载提示；`idle` 清理提示；`error/offline` 显示失败与重试，已有可用地图不主动清空。仅用户重试时 `setStyle`，无后台重试循环或缓存穿透参数。 |
| 退出清理 | `pagehide` 清理计时器、ResizeObserver、指针状态并调用 `map.remove()` 释放 WebGL/worker；不添加定位、后台轮询或持久化。 |

标题、北标、电量、回中与缩放均围绕圆形布局；中心准星、圆弧与文字路径为可交互 UI 几何，常规图标来自 Feather。74% 为电量样例，北标表示固定地图方向，不读取方向传感器。

## 免费来源、依赖与取舍

当前使用 [OpenFreeMap](https://openfreemap.org/) 的免费、免 Key 公共矢量服务及 OpenStreetMap 数据，浏览器以固定版本 [MapLibre GL JS 5.12.0](https://github.com/maplibre/maplibre-gl-js/tree/v5.12.0) 渲染。版本为本次核查并实际验证的基线，不声称是最新版本。

`map-style.json` 由 [OpenFreeMap Dark](https://tiles.openfreemap.org/styles/dark) 定制：深蓝河流、灰色道路与楼体、低密度中文标注；保留原始矢量地理数据。原样式留在 `vendor/openfreemap-dark.original.json`，便于核对来源与恢复。真实地图的道路、楼体和文字与 AI 概念图存在内容差异，正式浏览以真实地图数据为准。

此前 OSM 标准图片瓦片无法独立调节水域、道路和标注，故此次改用免费矢量来源完成所选暗色方向。未采用高德或其他需要账户 Key 的服务，未创建账号或收费资源。浏览器可运行 MapLibre；ESP32 / LVGL 不能直接运行该 WebGL 库，后续固件显示链路、缓存、解码与实际国内网络性能须另行评估，本稿不代表真机可运行。

来源与许可证保存于 `vendor/`：

- MapLibre：`MAPLIBRE-LICENSE.txt`，BSD-3-Clause 及随包声明。
- Feather 4.29.2：`FEATHER-LICENSE.txt`，MIT；图标来自其官方 npm 包。北标三角只调整填色。
- 地图样式：`OPENFREEMAP-STYLES-LICENSE.md`、`DARK-MATTER-LICENSE.md`；代码 BSD-3-Clause / MIT、设计 CC BY 4.0，保留 CARTO / Stamen / Paul Norman 的原始来源说明。
- 地图数据：[OpenStreetMap ODbL](https://www.openstreetmap.org/copyright)。圆屏内持续显示 OpenFreeMap、OpenMapTiles、OpenStreetMap 署名，页面底部另保留来源与样式设计署名。无区域预取、离线下载或地图代理服务。

## 本次验证与边界

使用 Codex 内置浏览器实际验证：地图载入、原尺寸与窄屏的圆弧拖动、地图拖动和回中、± 缩放、4/18 级边界、键盘操作、上海/杭州切换、选择层焦点循环与 Esc、地图资源 404 后提示及恢复资源后点击重试。最终默认状态回到上海 `[121.4892, 31.239]`、14.2；右弧拖动后到约 16.8，地图中心保持不变。

900×840 视口中逻辑屏实际为 466×466；390×800 视口中为 331.50×331.50、页面宽度 390，无横向溢出。`node --check preview.js`、本地 JSON 与尾随空白检查通过。以上不是设备帧率、触摸或用户视觉验收。

地图失败测试仅临时移动本目录的样式文件，观察真实 404 与重试流程，之后已完整恢复；未改服务器、权限或外部数据。其他临时文件已清理，仅保留审阅图片、对照证据和当前预览服务。

## 纠正与防复发

- “全屏”应检查实际可见地图，不能只看容器尺寸。上下整幅遮罩会使地图实际只剩中间一带。
- 为圆屏设计应利用圆周组织文字和操作；普通横排控件改圆角仍不符合本次目标。真实比例、命中区域、圆形裁切和署名均需实际查看。
- AI 概念不能充当真实地图：地理细节与桥名不得作为事实照抄，选定风格要用真实数据实现。生成概念也要检查屏外文字； Quiet Lens 初稿的屏外“工具”已由 `quiet-lens.png` 修正替代。
- 固定 SDK 版本后核对实际 API。首轮误用未导出的 `supported()` 导致地图未初始化，已改为 SDK 存在检查及构造器捕获，后续实际初始化与交互通过。无 `supported()` 的版本不可沿用 Mapbox 等库的静态探测写法。
- 地图平移有惯性，中心描述在 `moveend` 更新；验证应等视野稳定，不把即时旧描述误判为拖动失效。
