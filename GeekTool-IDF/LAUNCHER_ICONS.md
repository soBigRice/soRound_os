# 启动器图标

## 当前实现：A 黑白实心（2026-10-19）

用户确认：「就用 A 黑白实心这套，全部替换好，重点检查大小、居中和错位问题。」
20个现有应用和整合新增的生态箱/专注/声音胶囊均已使用A套实心造型；来源和确认原话见
[设计与接管记录](./artwork/launcher/icon-design-20261019/README.md)。
当前已完成源码接入与原生验证；设备AMOLED观感、触摸和最终实现仍待用户验收，未烧录、提交或发布。

### 尺寸、对齐与不变项

- 图标画布148×148，可见长边116～117px，选定稿的光学外框偏移最多3.5px；使用选定A稿的透明内容边界和有界光学校准，不在固件里二次缩放、重拟合或逐项加坐标偏移。
- `companion_icons.c` 以独立适配层扩展3个新入口，原20项枚举不重排；`launcher.c` 仍以196×196按钮居中上移16px，图标使用 `lv_obj_center`，名称、注册顺序、点击范围、左右箭头与220ms/56px切换沿用。
- `launcher_icons.c / draw_mask` 从 `lv_obj_get_coords` 得到的实际屏幕原点绘制148×148资源；父容器平移、合成层及图像使用同一坐标系，避免把局部坐标混成屏幕坐标。
- 木鱼保留已确认的实心曲线和斜向开口。少量红色按A稿保留，不扩大到其他页面。

### 素材转换与绘制调用链

`selected/*.svg → tools/gen_launcher_icons.cjs → sharp RGBA → 白/红A8透明度 → LVGLImage.py A8/RLE → main/launcher_artwork.inc`

`launcher_start → launcher_icon_create → launcher_icon_set → icon_event → paint → draw_mask → lv_draw_image`

选定稿固定在 `artwork/launcher/icon-design-20261019/selected/`，`manifest.json` 保存20项枚举顺序和源SVG摘要；
生成器校验摘要、画布、颜色和枚举，任何新应用或重排都必须显式同步素材，不能静默画错图标。
通用轮廓来自Phosphor Icons 2.1.1（MIT），许可证与原始SVG随设计稿保留。

每个资源使用现有A8/RLE解码器，白色重着色为 `0xf5f5f2`，红色为 `COL_RED`；
红白交界按source-over反解底层透明度，避免两张遮罩在抗锯齿边缘混合发黑。
20张白色遮罩和5张红色遮罩的压缩像素共62871B，每张解码像素为21904B；
全部25张纯像素合计547600B，实际驻留由现有1MiB全局缓存管理。
没有新增SVG解析器、字体、线程、存储分区或缓存配置；相比直接RGBA，每张A8遮罩的解码像素占用为四分之一。

切换继续由 `swap_exec` 对父按钮应用 `opa_layered`，先合成完整图标再整体淡出；
不分别淡出白色和红色，否则彩色交界会改变混合比例。
原生API只保留28px导航箭头的3px笔画，应用图标不再用线段和圆弧拼接。

转换命令（现有Node+sharp，Python+pypng/lz4）：

```sh
node GeekTool-IDF/tools/gen_launcher_icons.cjs [python路径]
```

格式与解码依据：[LVGL9.5官方离线图像转换器](https://lvgl.io/docs/open/9.5/main-modules/images/adding_images.html)，
并已核对本地9.5 `lv_draw_sw_img.c` 的A8着色分支；核查日期2026-10-19。

### 原生验证

`tools/preview_launcher_icons.c` 默认导出46张中英文页面和2张淡出帧；
`--verify` 额外复用同一个图标控件，依次验证23种入口的静止、50%父层合成、父容器(-37,+23)平移状态。
74%电量仅为固定夹具，合辑不是固件网格界面。

`tools/check_launcher_artwork.py` 将上述69种真实LVGL/RGB565状态与选定A稿PNG逐像素比较，
同时检查可见长边、边缘留白、位置和身份映射。旧线条实现已验证失败（首项像素误差246/255）；
替换后全部通过，最大误差12/255，处于RGB565及双A8混合量化范围，未通过放宽阈值修测试。
本地9.5的 `launcher_approved_artwork` 与6项启动流程测试全部通过。
当前23项原生合辑为 `native/review-23.png`；原生样例位于 [native/](./artwork/launcher/icon-design-20261019/native/)。

ESP-IDF6.0.1 / ESP32-S3隔离整包构建通过：基线 `80d4007` 叠加本次图标绘制与资源，
镜像4211792B，8MiB应用槽余4176816B；薄回执为 `native/validation.json`。
首个含并行改动的快照曾被 `capsule_wav.h:39` 的误导缩进告警阻断；该行随后已在原工作区被其他工作修正，
本任务没有修改音频代码。当前含其他并行修改的完整工作区未以本任务结果代为验收，整包通过只对应上述基线范围。

```sh
cmake -S GeekTool-IDF/tests/host -B /tmp/geektool-host-tests
cmake --build /tmp/geektool-host-tests --target preview_launcher_icons startup_flow_tests -j 6
ctest --test-dir /tmp/geektool-host-tests -R 'launcher_approved_artwork|startup_(complete|image_face|hand_mark|hand_dots|home_dma_failure|ui_lock_failure)' --output-on-failure
```

设备待验收：左右滑动及箭头遍历23项，点按进入/返回、连续快速切换和锁屏恢复；
检查真实AMOLED上的大小、视觉重心、边缘、红白交界和重影。原生渲染及整包构建不替代设备体验。

## 历史实现记录

以下为旧版本实现和发布证据；当前绘制路径以本页上文为准。

2026-10-13：用户确认木鱼改为白色实心轮廓和斜向开口，去除木槌、鱼眼与卷纹。
`artwork/merit/icon-design-20261013/merit-icon-v2.svg` 为已确认源图；
`tools/gen_merit_icon.js` 按148×148渲染，调用当前LVGL9.5官方 `scripts/LVGLImage.py`
生成1758B的无损RLE A8资源 `main/merit_icon.inc`。
`launcher_icons.c / LAUNCHER_MERIT` 通过 `lv_draw_image` 读取已有解码器并着色为 `INK`，
保持现有按钮、名称、事件与切换淡出。其他图标继续使用原生绘图；应用内木鱼图片、音频与计数不变。
原生中英文页面及50%淡出帧见该素材目录的 `native-merit-*.png`；对应启动流程回归已通过。

2026-10-09源码补充：第20项为“方块 / Pixels”，原19项顺序不变；图标由七个白方块与一个偏移红方块组成。
`preview_launcher_icons`的枚举/名称同步追加；以下已发布版本与历史19项审阅证据保留。
实现及原生验证见 [方块像素场](./PIXELS_UI.md)，已随beta.33发布；真实设备观感待验收。

核对日期：2026-10-05。启动器原16个App图标、左右切换箭头及图标边界沿用已发布实现；
新增第17个“答案之书”，使用白色书本轮廓与红色书签，追加在原注册表末尾。
天气页面已确认的天气资源、表盘、全局电量环和App业务行为沿用。

## 视觉与边界

`main/launcher_icons.c` 使用148×148画布、7px圆头笔画、`0xf5f5f2`主体及`COL_RED`关键细节。
图形主体约100～120px，按视觉重量调整；不是把每个图标硬拉伸成相同外接框。
Wi-Fi弧、扫描框、芯片、云/日、日历、沙漏、秒表、滑轨、上箭头、音频波形、
气泡水平仪、迷宫、水滴、骰子、鼠标、双立方体各有可辨识轮廓。
红色用于信号源、芯片核心、日期、指针、旋钮等语义细节，避免到处散落红点。

图标按钮保持196×196、圆屏中心上移16px，名字位置和点击进入语义沿用。
用户反馈beta.18的浅灰外圈不够清晰，本轮改为2px白色 `0xffffff`，名称从16px改为平滑24px。
名字使用已有`font_location_24`、300px居中宽度；左右箭头使用独立28px几何控件。
这里只替换图标表现，不将启动器变成图标网格。

## 绘制与切换调用链

`launcher_start → launcher_icon_create` 创建当前/候选两个图标控件。
`apply_app → draw_icon → launcher_icon_set` 更新枚举并使控件失效；
`nav → launcher_icon_set(g_nextart) → swap_exec` 在动画前备好下一图标，中点交换现有控件。
保留220ms、56px小面积切换、单个方向缓存、22px点击/滑动判定及原16个App注册顺序。
`launcher.c` 编译期校验App数量与 `LAUNCHER_ICON_COUNT` 一致；新增或调整注册顺序时须同步枚举。

每个图标是一个LVGL控件，`icon_event` 在 `LV_EVENT_DRAW_MAIN` 绘制。
线、圆弧、圆角矩形和点使用已有LVGL原生API；木鱼的实心曲线使用现有RLE解码器读取小型透明资源。
没有新增SVG解析器、字体或后台任务。
透明区域返回 `LV_COVER_RES_NOT_COVER`，手势/事件沿用 `lvgl_compat.h` 冒泡。
无效枚举不会绘制越界内容；当前App枚举与两种导航箭头有明确范围。

`swap_exec` 对图标按钮使用 `opa_layered`：先合成，再整体淡出，防止圆头线条交点叠加半透明后变亮。
LVGL Simple Layer按已有 `CONFIG_LV_DRAW_LAYER_SIMPLE_BUF_SIZE=24576` 分块管理临时缓存，静止时不创建该层。
依据本地LVGL9.5 `docs/src/main-modules/draw/draw_layers.rst` 和
`docs/src/common-widget-features/styles/style-properties.rst`，同时用9.6实测。
不为这套小尺寸原生图形引入完整SVG/位图渲染链；曲线使用解析圆弧，避免短整数线段拼接的边缘起伏。

## 原生预览与验证

`tools/preview_launcher_icons.c` 和 host CMake 的 `preview_launcher_icons` 目标导出真实LVGL的466×466 RGB565画面。
各19个中/英文页面及一个50%淡出帧；74%电量是预览夹具，不是设备当前电量。
图标合辑只是截取这些页面作排版，固件仍为单App居中启动器。

本地LVGL9.5/9.6各14组既有回归通过，断言启用；两版本均导出33张原生画面。
50%合成帧的主体最大通道值123，未出现交点二次叠加亮点。
ESP-IDF6.0.1本地构建通过，包体 `0x2f7010`（3,108,880B），4MiB槽余 `0x108ff0`。
CI使用LVGL `9.6.0~1` 构建的 `v1.7-beta.18` 已发布，包体3,142,096B，4MiB槽余1,052,208B。
GitHub/R2/国内完整字节一致，范围请求及国内续传校验通过；正式通道保持 `v1.6.1`。
本地9.5包不能替代CI发布包；USB未连接，设备启动、真实观感及触摸仍待设备验收。
beta.18源码 `0fca0bd`；本轮外圈/名称及设置/Wi-Fi修订见设置交互文档，beta.18摘要与下载证据见 [发布记录](./PORTING_NOTES.md#2026-10-04-v17-beta18-图标发布核对)。

2026-10-05：本轮白色2px外圈/24px名称随源码`d567e5f`发布beta.19，LVGL9.5/9.6各15组通过。
GitHub/R2/国内完整发布包及条件Range核对通过，旧3MiB同步上限已修正并部署；
不能将云端构建或本地原生预览视为设备已升级。后续状态见 [beta.19发布记录](./PORTING_NOTES.md#2026-10-05-v17-beta19-发布与镜像上限核对)。

原生OTA测试在开启优化时卡于9.5的 `lv_display_set_buffers` 对齐断言：
采样栈定位至 `lv_display.c:504`，`nm` 显示旧 `buffer` 地址尾为 `0xd2`，仅2字节对齐。
测试改为 `_Alignas(LV_DRAW_BUF_ALIGN)` 后地址尾为 `0xd4`，两版本全部回归通过。
这是原测试数组没有声明库所需对齐的缺口，不是此次图标或OTA固件回归；没有关闭断言或修改库的对齐要求。
新增预览控件的缓存同样显式对齐，后续native检查使用有界超时，避免断言halt进程持续占用CPU。

2026-10-05新增答案之书的页面、生命周期与原生回归见 [ANSWERS_UI](./ANSWERS_UI.md)。

设备验收：左右滑动和箭头遍历19个App，确认主体、名称与对应App一致；
点按进入、右滑返回、快速连续切换、锁屏后恢复，图标不重影、不裁切、手势不误进App。
真实AMOLED观感与触摸仍由用户验收。

2026-10-05：在答案之书后追加星座运势与木鱼，原17个App顺序不变。新图标分别采用星连线/红色亮点、木鱼轮廓/红色敲击棒；复用白色外圈和24px名称。功能与验收见 [星座与木鱼](./ZODIAC_MERIT_UI.md)。

2026-10-19合并发布补充：新增3项启动器原为Lucide描边且生态箱仅94px长边，现以同版本Phosphor实心源图校准至约116px，源图在 `additional/`，`tools/gen_remote_icons.py` 仅重生成这3项启动器；遥控台内部30px图标不变。69项像素状态与47项主机回归通过，适配层另外验证原20项像素和父层透明度逐字节相同。
