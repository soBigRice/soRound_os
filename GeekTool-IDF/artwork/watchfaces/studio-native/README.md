# STUDIO 九款 · 原生 LVGL 审图

2026-10-20。用户确认九款全部实现并强调高还原度。时间固定为2026-10-20 10:08:36，数据来自夹具，绘制来自生产`watchface_render`，不是把浏览器稿烧成静态整屏图片。

- [正常态九款](./active-overview.png)、[常显九款](./aod-overview.png)
- 原稿与原生并排：[器械](./compare-active-instrument.png)、[玩物](./compare-active-play.png)、[自然](./compare-active-nature.png)
- 中英设置页：首款[中文](./selector-21-zh.png)/[英文](./selector-21-en.png)，末款[中文](./selector-29-zh.png)/[英文](./selector-29-en.png)
- `active-*.png / aod-*.png`为独立466×466原生图；`reference-*.png`为批准离线SVG在Chrome中1倍截图，未改原稿。
- `comparison.json`按屏幕RGB565量化后记录正常/AOD误差，供检查定位，不把单一均值当作主观设计验收。

对照已恢复原稿字号和文字、日期格式、基线/字距、透明度、曲线及描边。浏览器与LVGL存在字体抗锯齿、颜色量化、路径接缝等固有差异；本轮没有访问设备，不能宣称真机显示或逐像素全等。布局及原生图已经逐款查看。

重建：`python3 GeekTool-IDF/tools/render_studio_review.py --faces <host-build>/watchface_tests --settings <host-build>/settings_level_tests`。脚本清理临时PPM，仅保留PNG与小型比较回执。源SVG在[设计对照目录](../exploration-20261020/README.md)，实现与测试链见[表盘说明](../../../WATCHFACES_UI.md)。
