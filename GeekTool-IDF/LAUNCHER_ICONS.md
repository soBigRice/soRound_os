# 启动器图标

核对日期：2026-10-04。用户要求重做当前图标，范围为启动器16个App图标、左右切换箭头及图标边界。
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
保留220ms、56px小面积切换、单个方向缓存、22px点击/滑动判定及App注册顺序。
`launcher.c` 编译期校验App数量与 `LAUNCHER_ICON_COUNT` 一致；新增或调整注册顺序时须同步枚举。

每个图标是一个LVGL控件，`icon_event` 在 `LV_EVENT_DRAW_MAIN` 绘制。
线、圆弧、圆角矩形和点使用已有LVGL原生API；没有新增图片包、SVG解析器、字体或后台任务。
透明区域返回 `LV_COVER_RES_NOT_COVER`，手势/事件沿用 `lvgl_compat.h` 冒泡。
无效枚举不会绘制越界内容；当前App枚举与两种导航箭头有明确范围。

`swap_exec` 对图标按钮使用 `opa_layered`：先合成，再整体淡出，防止圆头线条交点叠加半透明后变亮。
LVGL Simple Layer按已有 `CONFIG_LV_DRAW_LAYER_SIMPLE_BUF_SIZE=24576` 分块管理临时缓存，静止时不创建该层。
依据本地LVGL9.5 `docs/src/main-modules/draw/draw_layers.rst` 和
`docs/src/common-widget-features/styles/style-properties.rst`，同时用9.6实测。
不为这套小尺寸原生图形引入完整SVG/位图渲染链；曲线使用解析圆弧，避免短整数线段拼接的边缘起伏。

## 原生预览与验证

`tools/preview_launcher_icons.c` 和 host CMake 的 `preview_launcher_icons` 目标导出真实LVGL的466×466 RGB565画面。
各16个中/英文页面及一个50%淡出帧；74%电量是预览夹具，不是设备当前电量。
图标合辑只是截取这些页面作排版，固件仍为单App居中启动器。

本地LVGL9.5/9.6各14组既有回归通过，断言启用；两版本均导出33张原生画面。
50%合成帧的主体最大通道值123，未出现交点二次叠加亮点。
ESP-IDF6.0.1本地构建通过，包体 `0x2f7010`（3,108,880B），4MiB槽余 `0x108ff0`。
CI使用LVGL `9.6.0~1` 构建的 `v1.7-beta.18` 已发布，包体3,142,096B，4MiB槽余1,052,208B。
GitHub/R2/国内完整字节一致，范围请求及国内续传校验通过；正式通道保持 `v1.6.1`。
本地9.5包不能替代CI发布包；USB未连接，设备启动、真实观感及触摸仍待设备验收。
beta.18源码 `0fca0bd`；本轮外圈/名称及设置/Wi-Fi修订见设置交互文档，beta.18摘要与下载证据见 [发布记录](./PORTING_NOTES.md#2026-10-04-v17-beta18-图标发布核对)。

2026-10-05：本轮白色2px外圈/24px名称随源码`d567e5f`发布beta.19，LVGL9.5/9.6各15组通过。
GitHub/R2发布包核对通过，国内镜像仍为beta.18，旧3MiB同步上限修正待部署；
不能将云端构建或本地原生预览视为设备已升级。后续状态见 [beta.19发布记录](./PORTING_NOTES.md#2026-10-05-v17-beta19-发布与镜像上限核对)。

原生OTA测试在开启优化时卡于9.5的 `lv_display_set_buffers` 对齐断言：
采样栈定位至 `lv_display.c:504`，`nm` 显示旧 `buffer` 地址尾为 `0xd2`，仅2字节对齐。
测试改为 `_Alignas(LV_DRAW_BUF_ALIGN)` 后地址尾为 `0xd4`，两版本全部回归通过。
这是原测试数组没有声明库所需对齐的缺口，不是此次图标或OTA固件回归；没有关闭断言或修改库的对齐要求。
新增预览控件的缓存同样显式对齐，后续native检查使用有界超时，避免断言halt进程持续占用CPU。

设备验收：左右滑动和箭头遍历16个App，确认主体、名称与对应App一致；
点按进入、右滑返回、快速连续切换、锁屏后恢复，图标不重影、不裁切、手势不误进App。
真实AMOLED观感与触摸仍由用户验收。
