# soRound OS 官网

正式入口：**https://sobigrice.github.io/soRound_os/**。

本目录只维护产品官网，现有 `web/` 仍是独立的 GeekTwin 蓝牙校准器。
官网不请求设备权限，也不连接设备；表盘展示使用固定样例数据。

## 功能 UI 与硬件图片

`index.html:#explore` 展示全部19个应用，`script.js` 的第二个控制器只管理功能选项、
图片、标题、描述、序号和 `aria-pressed`，与表盘状态独立。默认天气预览；
天气、音频、木鱼另在黑色功能区直接展示。无 JavaScript 时保留这些图片和设备图。
`build_site.py` 检查19个选项不缺项、不重复，并校验每张应用 PNG 为466×466。

`assets/apps/` 使用当前 `main` 的真实 LVGL 控制器与既有主机夹具导出。
天气/设置/水平仪/音频/骰子/答案/系统/Wi-Fi/遥控台/木鱼/星座/OTA 复用现有原生回归导出；
`scripts/site_native/renderer.c` 链接真实日历、倒计时、秒表、迷宫、流体、I2C、数字孪生。
设备服务固定为离线夹具，应用 UI 不另画一套。额外 renderer 复用正式的 i18n 字体主题
与启动器标题/返回键/电量布局；遗漏主题 fallback 会使秒表“归零”变成缺字方框，生成前须检查。
日历固定2026-10-07，电量74%为夹具；Twin 为等待连接状态，截图不证明蓝牙或传感器连接。
既有夹具里的版本、天气和运势日期也都是样例，不代表当前发布版或实时数据。

需要更新界面时，从仓库根目录执行（沿用已有 CMake、Python/Pillow）：

```bash
cmake -S scripts/site_native -B /tmp/soround-site-previews -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/soround-site-previews --target site_native weather_ui_tests settings_level_tests audio_ui_tests dice_ui_tests answers_ui_tests system_ui_tests wifi_ui_tests remote_ui_tests merit_ui_tests zodiac_ui_tests ota_ui_tests -j 8
python3 scripts/render_site_apps.py --build-dir /tmp/soround-site-previews
```

导出脚本仅做 PPM→PNG 格式转换，不缩放或改画固件像素；临时原始图自动删除。
核对19张图片后删除本次 `/tmp/soround-site-previews`，不要删除其他任务的主机构建。

`assets/waveshare-1.75c.jpg` 原样保存微雪官方产品照片（460×345），来源：
[ESP32-S3-Touch-AMOLED-1.75C 产品页](https://www.waveshare.com/esp32-s3-touch-amoled-1.75c.htm)，
[原始图片](https://www.waveshare.com/img/devkit/ESP32-S3-Touch-AMOLED-1.75C/ESP32-S3-Touch-AMOLED-1.75C-8_460.jpg)。
版权归 Waveshare；官网图注明确标注“屏幕为出厂演示界面”，并链接官方产品页。
没有将官方照片中的出厂 AI/音乐 UI 改称 soRound OS 功能。

2026-10-07 用户补充：官网需要其他功能 UI 和实际微雪设备图。
此前完整功能文字列表仍不足以满足产品视觉展示；现补全可切换原生截图和官方硬件照片。
后续官网内容核对要同时检查表盘、功能 UI、硬件外观与素材来源，不以功能名列表代替 UI 展示。

本轮验证：既有11个原生 UI 目标全部通过并导出所需12个应用界面；另7个真实控制器
离线渲染成功。补齐正式字体主题后，秒表“归零”字形再次逐图核对。
Codex IAB 在1505×1045、390×844、320×740检查，无横向溢出；19个选项逐个点击，
图片路径、标题、序号与唯一选中状态匹配，Space/Enter 操作通过，无浏览器 error/warn。
手机选中后带回圆屏预览，缩略图进入对应应用，设备照片加载为原始460×345。
静态构建、PNG尺寸与完整目录、JavaScript语法、最终Diff检查通过。

## 本地预览

从仓库根目录执行（Python 3 标准库，无需安装依赖）：

```bash
python3 scripts/build_site.py
python3 -m http.server 8765 --bind 127.0.0.1 --directory build/site
```

访问 http://127.0.0.1:8765/。结束后 `Ctrl+C` 关闭服务器，可删除本次生成的
`build/site/`，不要清理其他固件构建目录。
需要核对当前公开发布版时用 `python3 scripts/build_site.py --refresh-releases`。

## 内容、预览和部署调用链

```text
site/index.html + styles.css + script.js + assets/
  → scripts/build_site.py
  → 替换正式/内测版本链接，检查锚点、资源、原生预览尺寸
  → build/site/（只包含公开站点资源）
  → .github/workflows/pages.yml
  → upload-pages-artifact → deploy-pages → GitHub Pages
```

- `main` 上的官网相关修改、GitHub Release 发布/编辑、手动运行都会触发部署。
  发布事件仍明确取 `main` 的官网，不取可能尚无官网的旧固件 Tag。
- Pages 使用 GitHub Actions 发布源；无需 `gh-pages` 分支和额外构建框架。
- `github-pages` 环境允许 `main` 分支和 `v*` Tag。Release 事件的执行 ref 是 Tag，
  即使 checkout 了 `main`，只有分支策略仍会阻止部署；Tag 策略用于已有固件发布事件。
- CI 读取公开 GitHub Releases，以发布时间区分最新正式版和内测版。API 失败时
  构建失败，已上线版本保留；本地离线预览使用 `releases.json` 中的已核查快照。
- `build_site.py` 只删除和重建本项目 `build/site/`，不接受其他清理目标。
- 固件构建、Tag、Release 资产上传、OTA 地址与蓝牙协议均独立。
- 不上传源码目录、设计稿、README、固件构建文件或本地配置。

## 表盘素材与交互

`assets/type.png`、`orbit.png`、`shift.png` 是已有
`GeekTool-IDF/artwork/watchfaces/native/` 的原生 LVGL 审阅合辑副本。
网页用圆形 viewport 显示其中一个 466×466 区域，保持原始像素；
不是实机照片，温度、Wi-Fi、时间、电量都是夹具数据。

`script.js` 管理主题 `type/orbit/shift` 和用途索引 `0–4`。切换主题保留用途；
切换用途更新素材裁剪位置，并同步按钮 `aria-pressed` 与预览的无障碍名称。
主题和用途按钮使用原生按钮，Tab、Enter、Space 可操作；无定时器、后台请求或第三方统计。
图片失败有可见提示，无 JavaScript 时保留默认 ORBIT 环形预览与所有导航/下载链接。

合辑区域依次为：`(60,145)`、`(607,145)`、`(1154,145)`、`(334,709)`、`(881,709)`；
每个区域 466×466，合辑 1680×1220。更新合辑时必须一起核对这些坐标。
页面应用数量已核对 `main/launcher.c:APPS`，表盘组合已核对 `WATCHFACES_UI.md`。
Barlow 字体复用项目已有素材，OFL 许可证随 `assets/OFL.txt` 发布。

## 设计与验收

设计来源是内置 Image Gen 的三张 section 设计稿，保留在 `design/01-hero.png`、
`02-showcase.png`、`03-start.png`，仅用于维护比对，不属于发布文件。
生成要求：中文工业感产品官网，浅灰首屏/黑色表盘区/白色安装区，黑白红配色，
原生圆屏预览、开放布局、可实现的 HTML 控件、无虚构硬件照片和产品指标。

设计变量：首屏 `#f4f5f5`、展示区 `#131416`、安装区纯白、强调 `#e54330`；
正文 PingFang SC / 系统中文字体，英文和数字 Barlow；桌面内容宽度 1280px。
首屏和安装区标题使用 Google Fonts 官方 Noto Sans SC 900 的16字 WOFF2 子集，实际字重
不依赖系统中文字体的合成加粗。字体本地加载，无 Google Fonts 运行时请求；
来源为 [Google Fonts Noto Sans SC](https://github.com/google/fonts/tree/main/ofl/notosanssc)，
生成接口 `fonts.googleapis.com/css2?family=Noto+Sans+SC:wght@900&text=把灵感，装进圆里。准备好让屏醒来`，
2026-10-07 核对并保留 `assets/NotoSansSC-OFL.txt`；更改标题需重新生成子集。
移动端按内容单列排列，参数条变为两列；`prefers-reduced-motion` 禁用滚动/按钮动效。

事实所需的明确设计补充：使用真实固件预览替代生成图里的近似表盘；补齐全部19个应用、
正式/内测范围、版本链接与首次烧录/分区边界。网站展示不构成固件或设备体验验收。

部署方案核查日期：2026-10-07。使用 GitHub 官方
[Pages 工作流](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages)
和 [Pages API](https://docs.github.com/en/rest/pages/pages#create-a-github-pages-site)，
采用 `configure-pages@v5`、`upload-pages-artifact@v4`、`deploy-pages@v4`。
静态站点无需增加 React/Vite 或站点生成器依赖；现有校准器继续使用原框架。

### 2026-10-07 发布前核对

使用 Codex IAB 实际渲染、点击与 DOM/CSS 检查，没有使用 Mock 或 Playwright 替代浏览器。
首屏按设计稿原尺寸 1505×1045 检查，另检查 390×844 和 320×740；无横向溢出。
三个 section 的设计稿和最终浏览器截图均用 `view_image` 逐项对照。

| 对照项 | 设计与浏览器证据 | 处理 |
| --- | --- | --- |
| 首屏文案 | 品牌、四个导航、两行标题、两行正文、两项操作和预览说明 | 文案核对一致，没有增加眉题、徽章或指标 |
| 首屏比例 | 初稿标题偏小、首屏过高；最终标题 top≈189px，黑色区 top≈918px | 调整字号、真实字重、留白和圆屏比例，恢复设计的首屏节奏 |
| 配色/容器 | CSS 实测 `rgb(244,245,245)`、`rgb(19,20,22)`、`rgb(255,255,255)` | 保留浅灰/黑/纯白开放布局，无多层卡片与图片染色 |
| 原生表盘 | 生成稿是近似刻度；现有合辑中的真实 ORBIT 刻度与其不同 | 有意使用真实固件绘制，不把生成稿当固件截图 |
| 字体/控件 | 英文 Barlow；大标题本地 Noto 900 子集；中文正文系统字体 | 修正系统字重不足；按钮、选择项、正文均显式定义字号 |
| 安装区 | 两列硬件/步骤、三个红色序号、黑/白两个操作 | 保留设计，增加真实正式/内测链接与首次安装边界 |
| 小屏 | 首屏/表盘/安装区变单列，参数两列，320px 下载操作自动逐行 | 原生预览保持圆形、长型号自然换行，内容不裁切 |

三主题 × 五用途逐个实际点击，15组素材、裁剪区域、选中状态与无障碍名称匹配；
Space 选择主题、Enter 选择用途通过，主题切换保留用途。浏览器无 error/warn。
静态构建、公开版本查询、JavaScript 语法、锚点/资源检查、`git diff --check` 通过；
构建脚本拒绝 `--out build`，未删除其他构建目录。
核心交互和主体视觉已按设计基线核对；上述事实补充与真实固件素材替换是有意差异。
网站主观效果仍由用户确认，未将自动检查表述为用户验收。

### 首次上线核对

2026-10-07：GitHub Pages 已设为 `workflow` 发布源并强制 HTTPS；
官网实现提交 `c3c0590` 的 [Actions 部署](https://github.com/soBigRice/soRound_os/actions/runs/37596692357)
成功。公开站点14个文件均返回 HTTPS 200，SHA-256 与本地发布目录一致；
README 与仓库 About 的 homepage 已补上官网链接。线上文件核验与本地浏览器功能检查
分别执行，不将部署状态代替真实访问。
