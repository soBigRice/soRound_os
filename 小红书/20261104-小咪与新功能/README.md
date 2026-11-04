# 小咪、自选行情与自动演示 · 小红书图文

2026-11-04制作。基于当前本地 `main`（HEAD `e24a541`）及既有未提交的股票、演示功能；只新增本目录，不修改固件、不提交、不推送或发布。

## 直接使用

- `发布文案.md`：一篇主帖，含标题备选、正文与话题。
- `拆分短帖.md`：宠物与行情两篇独立稿，注明各自取图顺序。
- `发布配图/`：01—08，1080×1440，3:4。按编号上传 JPG；PNG 是无损备份，不要重复上传。
- `00-配图总览.jpg`：审稿总览，不作为单张发布图。
- `index.html`：本地图片总览。
- `排版源文件/`：HTML、CSS、素材复制与排版脚本。
- `素材原图/` 与 `素材清单.json`：原始PNG副本、来源路径与SHA-256，不包含设备备份、凭据或日志。

## 本次采用的经验

按用户的小红书技能及现行经验库：封面用明亮底色与黑白主体形成对比，大字直接说明“小圆屏养了只小咪”；内页一页一个主题，面向非专业读者解释可做什么，减少零碎小字与广告口吻。没有将原经验擅自改写成新运营规则，也未修改技能或经验库。

来源：`/Users/superrice/.codex/skills/xiaohongshu-traffic-learning/` 的 `references/content-experiments.md`、`references/content-and-follow.md`，经验库 `INDEX.md` 所指L001—L004。

## 功能事实与图像边界

| 内容 | 核对依据 | 文案与画面边界 |
| --- | --- | --- |
| 小咪 | `GeekTool-IDF/MIMI.md`、`main/mimi_pet.c`、`main/app_mimi.c`、`artwork/mimi/fullscreen-native/` | 离线，25套眼睛、14段动作，轻点/滑动/长按返回、困倦唤醒；不宣传AI对话或保证帧率。形象和动作说明开源来源，不冒充原创设计 |
| 自选行情 | `GeekTool-IDF/STOCKS.md`、`main/app_stocks.c`、`tests/stocks/ui_tests.c` | 使用当前A版`stocks-A-20261103`截图，弃用旧卡片版；最多8只、三行滚动、整份刷新、独立失败。截图为固定样例，行情可能延迟，不作实时性承诺 |
| 演示模式 | `GeekTool-IDF/DEMO_MODE.md`、`main/demo_mode.c::chapters` | 24个应用、62款表盘及主要子功能，共138场景；目录未含小咪和股票。第07页是原生界面与切换流程示意，不冒充演示运行截图；实际模式保留DEMO标记、使用独立样例 |
| 公开状态 | `GeekTool-IDF/RELEASE_NOTES.md`、上述三项模块说明 | 三项均为本地开发版，未公开发布；开源地址指现有公开项目，不声称已能下载本帖新固件 |

项目地址沿用历史确认写法 `GitHub · soBigRice/soRound_os`，个人主页 `sobigrice.com`。未登录小红书、未上传或发布，未产生付费AI图片调用。

小咪第三方来源：

- [nasawz/GrokBot](https://github.com/nasawz/GrokBot)，BSD-3-Clause，固定提交 `bb7b634b029a6c99121cb74954020ddfc8c07acf`。
- [jeremy-prt/bloub](https://github.com/jeremy-prt/bloub)，MIT，固定提交 `b4bb3c1b5f93c7b87a2e8d620f667c4093d97749`。

副本许可在 `素材原图/第三方许可/`；图片只缩放、按原圆屏边界排版，不重绘界面或虚构真机照片。

## 重建与验证

`python3 排版源文件/build_pack.py` 重建HTML和素材副本；原始路径见脚本与清单。图片通过本地浏览器1080×1440导出，JPG质量95，源稿无远程字体与网络请求。中文字体使用macOS系统PingFang SC，未复制分发系统字体。

导出后检查每张页面边界、图片加载、文字溢出、内容与页脚间距、原图摘要、格式尺寸与压缩包内容。实际记录见 `验证回执.json`。本次只验证图文，不重新测试固件，也不继承历史真机验收。最终语气与视觉效果待用户确认。
