# STUDIO 九款 · 原生 LVGL 审图

> 2026-10-20：[beta.37](https://github.com/soBigRice/soRound_os/releases/tag/v1.7-beta.37)已发布，STUDIO九款恢复并保留原21款；beta.36继续撤回。下列图片是设计与实现对照，probe3设备轮测和精确CI身份已核验，精确发布包USB安装、70秒普通固件启动观察及新鲜VALID读回均通过，原数据与beta.35恢复槽校验保持。见[恢复记录](../../../PORTING_NOTES.md#2026-10-20-beta36撤回与旧槽恢复)及[组合验证](../../../PORTING_NOTES.md#2026-10-20-beta37组合修复与验证)。

2026-10-20。用户确认九款全部实现并强调高还原度。时间固定为2026-10-20 10:08:36，数据来自夹具，绘制来自生产`watchface_render`，不是把浏览器稿烧成静态整屏图片。

- [正常态九款](./active-overview.png)、[常显九款](./aod-overview.png)
- 原稿与原生并排：[器械](./compare-active-instrument.png)、[玩物](./compare-active-play.png)、[自然](./compare-active-nature.png)
- 中英设置页：首款[中文](./selector-21-zh.png)/[英文](./selector-21-en.png)，末款[中文](./selector-29-zh.png)/[英文](./selector-29-en.png)
- `active-*.png / aod-*.png`为独立466×466原生图；`reference-*.png`为批准离线SVG在Chrome中1倍截图，未改原稿。
- `comparison.json`按屏幕RGB565量化后记录正常/AOD误差，供检查定位，不把单一均值当作主观设计验收。

对照已恢复原稿字号和文字、日期格式、基线/字距、透明度、曲线及描边。浏览器与LVGL存在字体抗锯齿、颜色量化、路径接缝等固有差异；这些图片最初导出时没有设备访问，不能用它们宣称真机显示或浏览器与LVGL逐像素全等。布局及原生图已经逐款查看。

## beta.37的像素与设备边界

事故备份中的选中项是RADAR(ID21)。共享scratch候选将扫描数组移到唯一2084B BSS工作区，保留所有采样、透明度、路径和刷新频率。LVGL9.6.0~1本地probe实际目标ELF的polygon/ring helper栈帧分别由2368/1232B降为288/288B；最初两轮probe沿用7168B任务栈。

另做了独立的原header/候选header对比：冻结同一测试夹具和其余对象，48行RGB565下89/89帧逐字节一致，40行双缓冲RGB565_SWAPPED下也89/89帧一致；各组覆盖30款正常态、30款AOD和固定日期STUDIO参考。这是同格式内修改前后比较，不与`comparison.json`的SVG/原生误差混用，也不拿跨格式差异当scratch回归。

probe1安装的NEW选择经实际运行后已读回VALID；本轮没有失败自动回滚测试。probe2完成287.6秒九款全屏、设置预览及RADAR70秒/AOD跨分钟等阶段，未见fault，但设置预览后的历史最低可用栈仅424B，不能作为最终生产验收。

beta.37保持scratch及画面不变，将LVGL任务栈显式设为9216B，额外成本为2048B内部RAM，作为独立余量保护。probe3在真实launcher进入/返回链完成三轮39次预览、九款全屏/AOD、跨分钟与sleep/wake，约283秒到达DONE，无监测fault；9216B栈实际最低余量2952B，开机内部RAM最低73451B。标签CI及R2/国内完整包和条件Range已核验；精确发布ELF保持相同helper帧/工作区/任务栈，且无`device_probe`符号。精确公开包USB安装后，普通生产固件观察70.0955秒无fault，新鲜读回seq44/ota_1及seq43/ota_0均VALID(2)、两CRC有效；启动可用栈3256B，内部RAM最低73435B，原数据和beta.35恢复槽校验保持。不能把加栈或静态截图表述为事故首次坏写已被直接捕获。本轮未测网络OTA或失败自动回滚，主观观感、触摸与扬声器仍待用户验收；完整调用契约与证据入口见[表盘说明](../../../WATCHFACES_UI.md#beta36事故与beta37验证)。

重建：`python3 GeekTool-IDF/tools/render_studio_review.py --faces <host-build>/watchface_tests --settings <host-build>/settings_level_tests`。脚本清理临时PPM，仅保留PNG与小型比较回执。源SVG在[设计对照目录](../exploration-20261020/README.md)，实现与测试链见[表盘说明](../../../WATCHFACES_UI.md)。
