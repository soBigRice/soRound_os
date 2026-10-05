# 锁屏表盘：三组十五款

核对日期：2026-10-05。用户选择保留三个视觉方向，已实现固件与原生主机预览；
首次十五款实现已随本地 OTA 页面修复固件通过 USB 写入设备并正常启动；
实现已提交并随 `v1.7-beta.24` 内测包发布；设备已通过 OTA 从本地 beta.23 修复候选升级、
校验和重启，随后更新检查成功。主观效果、真机触摸/亮度/功耗尚待验收。
此前本轮无损字体压缩随获批的 TLS PSRAM 固件通过 USB 写入当前应用分区并正常启动，
该 USB 写入的分区表与 OTA 元数据前后完全一致；本次 OTA 则按正常流程切换启动槽。

## 选择与兼容边界

| 组别 | NVS 索引 | 五种表盘 |
| --- | --- | --- |
| TYPE | 0–4 | dots / bold / rings / weather / image |
| ORBIT | 5–9 | dots / bold / rings / weather / image |
| SHIFT | 10–14 | dots / bold / rings / weather / image |

`settings.c`原有`uint8_t face`及NVS键保持；索引0–4继续对应原来的五种用途。
设置 → 显示与表盘 → 表盘：左右切换全部十五款，底部三个组名可直接跳组，保留当前用途。
选择立即生效、保存；不在表盘上增加手势，也不改变BOOT短按锁/解锁、长按关机和PWR计时控制。

`watchface_name/theme_name/kind_name`是选择页的名称入口。缩略图为233px真实绘制，
不使用天气图标或假“12:34”代替表盘；设置页已有App计时器每秒调用`watchface_refresh_preview`，
缩略图没有独立计时器，重建/退出由父面板销毁。

## 调用链与状态

```text
lock.c:lock_set → watchface_show/hide
  → watchface.c:snapshot → 同一时间 / Wi-Fi / power_read / weather_cached / img_store 缓存
  → watchface_ui.c:watchface_render → 主题与用途组合 → 原生LVGL绘制

app_settings.c:pick_face / pick_theme
  → watchface_select → settings_set_face → settings_save → queue_rebuild
  → watchface_create_preview → 共用watchface_render
```

每个表盘只有一个绘制对象，没有为每个数字/刻度创建大量控件。
日期、SSID、IP、电量、温湿度及高低温均来自运行状态，预览中的主机样例不能当设备遥测。
SSID最多32字节，中心区域允许换行；Wi-Fi图标与长SSID错开。电量读取失败显示`--%`，
断网清空IP；天气失败明确显示不可用，温度不会冒充0℃。
天气复核包含高低温独立变化，修正了原表盘只比较温度/湿度/天气码而遗漏范围变化的问题。

| 状态 | 刷新 / 资源行为 |
| --- | --- |
| 活动态 | 点阵款秒点每秒更新；其他静态款按分钟或缓存变化重绘 |
| AOD | 秒点停止，图片背景隐藏；计时器对齐下一分钟，静态时间/日期仍显示 |
| 熄屏 | 删除表盘计时器，保留遮挡底层触摸的根对象 |
| 唤醒 | 恢复计时器、强制读取并补画当前时间 |
| 隐藏 | 删除表盘计时器；不在后台继续绘制 |

亮度和充放电熄屏判断继续由`lock.c`控制；未据主机渲染宣称功耗提升。

## 字体、图像与取舍

采用原生LVGL绘制及既有天气图片，不增加SVG引擎或UI依赖。
`watchface_ui.c`只为锁屏维护新的圆点数字，`glyph.c`中其他计时App字形保持。
实体数字使用OFL许可Barlow，4bpp字形只导出数字/标点和小号文字；原中文fallback沿用`font_cn16`。
2026-10-05 为恢复 OTA 槽空间，在原 Pillow 像素上采用 LVGL 原生无损 RLE/XOR 存储；
压缩无收益的7/9px字体保留原存储。字体生成需要 Python/Pillow 与 Node，使用
`tools/vendor/lv_font_conv` 固定 commit 的官方 MIT 编码器，没有 npm 或固件运行时依赖。
`watchface_tests:font_bitmap_digests`核对13字号全部501字形的像素和度量；
压缩前后38张466×466原生帧逐字节一致，设置缩略图回归通过。实际设备的刷新观感仍待验证。
没有导出整套大字库、降低字形灰阶或把整张表盘烧成图片。

2026-10-05核查[Barlow官方来源](https://github.com/google/fonts/tree/6cdf01867df0813c2390f90dff7dc66c87f14cf7/ofl/barlow)
及[OFL许可](https://github.com/google/fonts/blob/6cdf01867df0813c2390f90dff7dc66c87f14cf7/ofl/barlow/OFL.txt)；
原字体、许可证、commit和SHA256位于`artwork/watchfaces/fonts`。
完整许可也通过字形对象`user_data`保留在固件中。
没有沿用系统字体作为新增大字库输入，以便随固件分发来源明确。

三张默认背景由内置Image Gen依据用户选择的三张设计稿生成；
生成约束为466×466方形、无文字/钟面、顶部留黑、分别为雾中山峦/月下山峦/斜向岩石山体。
使用既有ImageMagick转换为466×466基线JPEG、4:2:0、quality=90，不改变构图有效内容。
最终文件为`artwork/watchfaces/type.jpg / orbit.jpg / shift.jpg`，设计目标保存在`reference/`。
`gen_watchface_backgrounds.py`嵌入JPEG，因此OTA也携带默认图片，无需另刷storage分区。

`img_store_face_image_for(theme)`先等原`bg.jpg`异步解码；自定义图片可用时优先使用，
缺失/解码失败后才启用当前主题默认图。每主题最多一个解码任务与一份RGB565缓存，
三个默认缓存的上限为1,302,936字节PSRAM；任务结束自行退出，没有图像重复解码循环。
任务/内存/解码/尺寸失败结束加载并显示失败文字；默认图严格验证466×466。
图像只作适度统一压暗；时间/日期区域较亮时提高压暗比例保证可读，不覆盖矩形黑卡。
AOD隐藏图片；原自定义`bg.jpg`文件不会被覆盖。

## 绘制经验与回归

LVGL的圆弧角度字段为无符号值，钟面以顶部为零点的`-90°`必须归一化后提交。
直接传负角度会把少量分钟进度错误地画为大段圆弧；已用10:08的六点位置亮度检查复现旧画法失败、修正后通过。
宽小时分段使用平端，避免圆端填满相邻间隙。
依据[LVGL9.5原生绘制API](https://lvgl.io/docs/open/9.5/main-modules/draw/draw_api)
及本机9.5头文件核对；支持现有9.5与9.6兼容封装。

`tests/watchfaces/watchface_tests.c`：十五款可区分、AOD分钟对齐/秒内稳定、睡眠与跨小时唤醒、
索引边界、时分进度语义、高低温独立更新、无天气/负温、32字节SSID、低电量/读取失败、图片加载及亮色自定义图。
`image_store_tests.c`：自定义优先、每组只起一次任务、完成清理以及任务/分配/解码/尺寸失败；
这里的codec是夹具，不冒充开发板ROM JPEG验证。
`settings_level_tests`：十五种缩略图、中英文圆形边界、跳组保留用途、14→0循环、保存/逐级返回和既有设置/水平仪回归。

实际JPEG格式/尺寸和字体SHA由`tests/watchfaces/check_assets.py`验证；
原生视觉复核见[TYPE](./artwork/watchfaces/native/type.png)、[ORBIT](./artwork/watchfaces/native/orbit.png)、
[SHIFT](./artwork/watchfaces/native/shift.png)、[选择页](./artwork/watchfaces/native/selector.png)、
[常显](./artwork/watchfaces/native/aod.png)。布局已查看；字体使用Barlow而非设计稿生成器的不可追溯字体，
照片为单独生成的同方向素材，非设计稿截图裁切。主观体验待用户验收。

重建和验证：

```sh
python3 GeekTool-IDF/tools/gen_watchface_fonts.py
python3 GeekTool-IDF/tools/gen_watchface_backgrounds.py
cmake -S GeekTool-IDF/tests/host -B /tmp/soround-host
cmake --build /tmp/soround-host --target watchface_tests watchface_image_tests settings_level_tests buttons_tests -j 6
ctest --test-dir /tmp/soround-host -R 'watchface_|settings_controls_and_level_recovery|physical_button_mapping' --output-on-failure
python3 GeekTool-IDF/tests/watchfaces/check_assets.py
python3 GeekTool-IDF/tools/render_watchface_review.py --faces /tmp/soround-host/watchface_tests --settings /tmp/soround-host/settings_level_tests
idf.py -C GeekTool-IDF build
```

预览脚本在临时目录生成raw/PPM，完成即删除；保存的PNG是审阅产物。
最终固件构建通过：4,142,160字节，4MiB槽余52,144字节（约50.9KiB），未调整分区。
SHA256：`0248fa8b6a05ca4ea723e2da63080d5efdb014f6446f54e823a2b0847e36b404`。
四组目标回归、真实素材格式/哈希检查及最终Diff检查通过；未以此替代设备ROM解码或真机验收。
真机验收最小范围：选遍三组五款→检查圆边/图片加载与自定义图→空闲进入AOD→触摸唤醒→BOOT解锁。
特别核对Wi-Fi SSID、高低温、图片解码和常显亮度；尚未据自动测试标记为用户验收。
