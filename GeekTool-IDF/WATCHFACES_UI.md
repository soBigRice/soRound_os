# 锁屏表盘：四组二十一款

2026-10-08：当前工作区在旧十五款后追加 HAND 六款，索引15–20；用户已授权按设计开发。
原生466×466绘制、设置选择、逐秒刷新、常显和旧图片缓存回归通过，本地ESP32-S3构建通过。
用户已要求发布`v1.7-beta.32`内测版本，发布证据见[发布记录](./PORTING_NOTES.md#2026-10-08-v17-beta32-指针表盘流体双模式与固定迷宫关卡)。
新增六款尚未烧录或通过用户/设备观感验收；此前十五款的设备证据不覆盖新增六款。
[HAND原生图](./artwork/analog-play/native/hands.png)、[选择页](./artwork/analog-play/native/selector.png)；
流体双模式与固定迷宫关卡见[小游戏实现](./PLAY_UI.md)。

核对日期：2026-10-05。用户选择保留三个视觉方向，已实现固件与原生主机预览；
首次十五款实现已随本地 OTA 页面修复固件通过 USB 写入设备并正常启动；
实现已提交并随 `v1.7-beta.24` 内测包发布；设备已通过 OTA 从本地 beta.23 修复候选升级、
校验和重启，随后更新检查成功。主观效果、真机触摸/亮度/功耗尚待验收。
此前本轮无损字体压缩随获批的 TLS PSRAM 固件通过 USB 写入当前应用分区并正常启动，
该 USB 写入的分区表与 OTA 元数据前后完全一致；本次 OTA 则按正常流程切换启动槽。

2026-10-05 beta.24 用户指出图片仍是旧熊猫、部分排版未还原及重复感。
2026-10-06 用户确认发布本轮修正，已发布内测版本`v1.7-beta.25`；本轮尚未写入设备。
真实 LVGL 原生预览见文末，不将旧 beta.24 的启动结果当作本轮验收。

## 选择与兼容边界

| 组别 | NVS 索引 | 表盘 |
| --- | --- | --- |
| TYPE | 0–4 | dots / bold / rings / weather / image |
| ORBIT | 5–9 | dots / bold / rings / weather / image |
| SHIFT | 10–14 | dots / bold / rings / weather / image |
| HAND | 15–20 | mark / arc / numeral / orbit / frame / dots |

`settings.c`原有`uint8_t face`及NVS键保持；索引0–4继续对应原来的五种用途。
设置 → 表盘：左右循环全部二十一款，底部四个组名可直接跳组。
旧三组之间跳转保留用途；首次进入HAND选择mark，离开HAND进入旧组的dots，同组点击保留当前款。
`watchface_catalog.c`集中管理名称与分组边界；HAND不套用旧`/5`与`%5`映射。
选择立即生效、保存；不在表盘上增加手势，也不改变BOOT短按锁/解锁、长按关机和PWR计时控制。

`watchface_name/theme_name/kind_name`是选择页的名称入口。缩略图为233px真实绘制，
不使用天气图标或假“12:34”代替表盘；设置页已有App计时器每秒调用`watchface_refresh_preview`，
缩略图没有独立计时器，重建/退出由父面板销毁。
2026-10-07用户要求表盘居中、设置项独立进入：预览固定在(116,116)，233px预览中心与466圆屏中心一致，
左右选择和底部主题组完整可见，不参与纵向滚动；直接从设置首页进入，返回恢复首页位置。
十五款渲染、索引与保存语义保持；[当前原生设置稿](./artwork/settings-detail/README.md)已本地验证并随beta.30发布，真实设备与观感待验收。

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
2026-10-07起网络状态采用 `wifi_service_ready`，AP已关联但DHCP/IP未就绪时不显示已联网；
原分钟/强制刷新时机保持，天气缓存共用同一后台请求。详见[联网恢复逻辑](./NETWORKING.md)。
天气复核包含高低温独立变化，修正了原表盘只比较温度/湿度/天气码而遗漏范围变化的问题。

| 状态 | 刷新 / 资源行为 |
| --- | --- |
| 活动态 | 旧点阵款和HAND六款每秒更新；其他旧款按分钟或缓存变化重绘 |
| AOD | 旧秒点停止、图片背景隐藏；HAND隐藏秒显示及附加信息，保留刻度/时分针；对齐下一分钟 |
| 熄屏 | 删除表盘计时器，保留遮挡底层触摸的根对象 |
| 唤醒 | 恢复计时器、强制读取并补画当前时间 |
| 隐藏 | 删除表盘计时器；不在后台继续绘制 |

亮度和充放电熄屏判断继续由`lock.c`控制；未据主机渲染宣称功耗提升。

HAND由`watchface_ui.c:hand_face`绘制：mark完整刻度/四数字；arc分段外圈/镂空时针；
numeral十二数字/柳叶针/小秒盘；orbit圆形时标；frame方形轨道/平头针；dots点阵刻度与真实日号。
时分针角度含秒/分进度，日期和电量来自同一`snapshot`，不触发天气与图片加载。
新增Barlow数字子集12/24 Regular、16/32 SemiBold由`gen_watchface_fonts.py --hands`生成，
沿用原OFL来源、4bpp无损编码，旧`font_watchface.c`及字形像素保持。

原生视觉复核发现：LVGL独立三角形各自抗锯齿，拼接会在实心针内部留缝。
`needle_polygon`改为凸轮廓逐行填充，仅外缘做四次采样覆盖，避免增加矢量引擎或图片资源。
`play_tests:native_hands`检查时针内部整段保持实心；不可只用每款hash不同判定视觉正确。

## 字体、图像与取舍

采用原生LVGL绘制及既有天气图片，不增加SVG引擎或UI依赖。
`watchface_ui.c`只为锁屏维护新的圆点数字，`glyph.c`中其他计时App字形保持。
实体数字使用OFL许可Barlow，4bpp字形只导出数字/标点和小号文字；原中文fallback沿用`font_cn16`。
本轮按设计稿区分字重与比例：堆叠大数字188px Black、横排时间104/112px SemiBold、
环形时间96px Regular、天气温度56px SemiBold；均有对应半尺寸字形供233px缩略图使用。
SemiBold同样取自下方固定commit，新增字体文件SHA256已纳入`manifest.json`和素材校验。
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

`img_store_face_image_for(theme)`先异步读取原`bg.jpg`，以旧出厂熊猫的长度22494与
CRC32 `8734e6d4`识别该静态素材，再决定是否解码。CRC调用ESP-IDF 6.0.1的
[ROM CRC API](https://github.com/espressif/esp-idf/blob/v6.0.1/components/esp_rom/include/esp_rom_crc.h)，
只用于旧默认素材识别，不作为可信性/安全校验。出厂熊猫不生成RGB565缓存，直接启用所选主题背景；
真正自定义图片仍优先，包括相同文件名、同样大小但内容不同的图片。
缺失/解码失败也使用主题默认图。每主题最多一个解码任务与一份RGB565缓存，
三个默认缓存的上限为1,302,936字节PSRAM；任务结束自行退出，没有图像重复解码循环。
任务/内存/解码/尺寸失败结束加载并显示失败文字；默认图严格验证466×466。
图像只作适度统一压暗；时间/日期区域较亮时提高压暗比例保证可读，不覆盖矩形黑卡。
AOD隐藏图片；原自定义`bg.jpg`文件不会被覆盖。

## 绘制经验与回归

此次遗漏：原实现把所有可解码的`/img/bg.jpg`视为自定义图，未区分随旧storage分区出厂的熊猫。
OTA保留storage，因此设备必然走旧图分支；此前主机预览直接注入三张主题图，绕开了这一真实入口。
防线改为用仓库实际`images/bg.jpg`做缓存控制器回归：修正前断言失败，修正后分别返回三组默认缓存；
同长度改单字节的图片及其他自定义图仍作为用户覆盖。只读挂载、不改文件、NVS和分区表。

设计复核必须对照`reference/`和同一`watchface_render`输出，不能以十五张hash不同或字形无损代替还原验收。
本轮修正TYPE点阵底部横向信息栏、亮刻度/小时点与环形大时间；ORBIT六段侧弧、矩形分钟刻度、
小时分段/圆形当前位置及红色冒号；SHIFT左对齐日期/图片时间、错位大数字、粗进度弧与剩余点轨/图例。
天气低高温恢复原稿箭头和分隔线，SHIFT保留红色分隔。长SSID仍按真实数据换行，三组都有32字节样例。
小时/分钟标记按真实时间计算；设计稿中示意标记位置不作为写死角度的依据。

LVGL的圆弧角度字段为无符号值，钟面以顶部为零点的`-90°`必须归一化后提交。
直接传负角度会把少量分钟进度错误地画为大段圆弧；已用10:08的六点位置亮度检查复现旧画法失败、修正后通过。
宽小时分段使用平端，避免圆端填满相邻间隙。
依据[LVGL9.5原生绘制API](https://lvgl.io/docs/open/9.5/main-modules/draw/draw_api)
及本机9.5头文件核对；支持现有9.5与9.6兼容封装。

`tests/watchfaces/watchface_tests.c`：二十一款可区分、HAND逐秒刷新且不访问图片/天气、AOD分钟对齐/秒内稳定、睡眠与跨小时唤醒、
索引边界、时分进度语义、高低温独立更新、无天气/负温、32字节SSID、低电量/读取失败、图片加载及亮色自定义图。
`image_store_tests.c`：真实出厂熊猫识别、同长度不同内容的自定义优先、每组只起一次任务、完成清理及任务/分配/解码/尺寸失败；
这里的codec是夹具，不冒充开发板ROM JPEG验证。
`settings_level_tests`：二十一种缩略图、中英文圆形边界、HAND跳组、14→15和20↔0循环、保存/逐级返回和既有设置/水平仪回归。

实际JPEG格式/尺寸和字体SHA由`tests/watchfaces/check_assets.py`验证；
原生视觉复核见[TYPE](./artwork/watchfaces/native/type.png)、[ORBIT](./artwork/watchfaces/native/orbit.png)、
[SHIFT](./artwork/watchfaces/native/shift.png)、[选择页](./artwork/watchfaces/native/selector.png)、
[常显](./artwork/watchfaces/native/aod.png)。布局已查看；字体使用Barlow而非设计稿生成器的不可追溯字体，
照片为单独生成的同方向素材，非设计稿截图裁切。主观体验待用户验收。
本轮LVGL 9.5/9.6各三项主机回归（表盘/背景缓存/设置预览）及素材校验通过；目标ESP32-S3固件构建通过。
本地ESP-IDF 6.0.1 / LVGL 9.5候选大小4,053,696字节，4MiB槽余140,608字节；
正式CI使用LVGL 9.6，发布时仍须以对应CI二进制的槽检查为准。TLS PSRAM配置及编译保护保持。
beta.25实际CI使用LVGL 9.6.0~1 / esp_lvgl_port 2.9.0，发布包4,087,360字节，槽余106,944字节。
GitHub/R2/国内OTA全文摘要相同，国内条件Range验证通过；完整版本/摘要及通道状态见
[发布记录](./PORTING_NOTES.md#2026-10-06-v17-beta25-表盘纠正发布)。
当前未检测到USB设备，本轮真机显示尚未验证；beta.24的OTA稳定性验证不自动涵盖这轮视觉修改。

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
下列大小与摘要属于此前十五款修正的构建，不代表2026-10-08新增HAND的镜像；当前构建证据见[小游戏实现](./PLAY_UI.md)。
此前固件构建通过：4,142,160字节，4MiB槽余52,144字节（约50.9KiB），未调整分区。
SHA256：`0248fa8b6a05ca4ea723e2da63080d5efdb014f6446f54e823a2b0847e36b404`。
四组目标回归、真实素材格式/哈希检查及最终Diff检查通过；未以此替代设备ROM解码或真机验收。
真机验收最小范围：选遍三组五款→检查圆边/图片加载与自定义图→空闲进入AOD→触摸唤醒→BOOT解锁。
特别核对Wi-Fi SSID、高低温、图片解码和常显亮度；尚未据自动测试标记为用户验收。
