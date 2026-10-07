# soRound OS 官网

正式入口：**https://sobigrice.github.io/soRound_os/**。

本目录只维护产品官网，现有 `web/` 仍是独立的 GeekTwin 蓝牙校准器。
官网不请求设备权限，也不连接设备；表盘展示使用固定样例数据。

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
