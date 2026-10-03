# 天气页：彩色 Nothing 风格 / 466×466 AMOLED

核对日期：2026-10-03。用户已确认彩色效果稿并授权实现；代码、主机渲染和固件构建的
验证见下文，设备视觉仍待验收。效果稿不是原生像素设计文件，不把渲染预览称为真机照片
或逐像素相同的验收结果。

## 目标与边界

- 保留已确认的城市 → 天气图标 → 点阵温度 → 天气描述 → 低高温 → 湿度层级。
- 点阵是图形风格，AMOLED 使用灰蓝云层、暖琥珀太阳、雾蓝降水、冰蓝雪与淡金闪电。
  正文使用局部平滑字体，不替换其他 App 的既有字体。
- 城市仍为 `Shanghai` / 固定经纬度；红色城市标记不新增定位权限、城市选择或点击行为。
- 数据接口、Wi-Fi 单任务保护、表盘缓存 API、成功后 20 分钟/失败后 1 分钟后台轮询保持原契约。
  本次仅给 current 查询增加 `is_day`，月亮不依赖本地固定时段推测昼夜。
- 电量环仍表达真实电量、低电与充电状态；天气页仅调整粗细与背景，退出后恢复。
  保留并行 OTA 工作的隐藏电量层、点阵返回键和导航逻辑。

## 入口与状态流

`launcher.c:enter_app` → `header_app_style` 应用天气页局部顶栏样式 →
`app_weather.c:weather_enter` → `weather_ui_create` → `start_fetch`。

`wx_task` 保持独立 HTTP 任务：读取 current/daily，解析 `temperature_2m`、湿度、天气码、
`is_day` 和当日低高温 → 写入缓存 → 增加 `s_revision` → 发布 `WX_OK`。
`weather_tick` 比较状态及数据更新序号，再调用 `weather_ui_show` 或 `weather_ui_status`。
UI 绘制只在 LVGL 线程发生，退出清空控件引用，后台任务仍可完成并供表盘使用。

加载/失败时隐藏正常描述及次要数据；首次无数据用灰点和 `--` 表示，不伪造 0℃ 或晴天。
未知天气码保留数据、显示不可用描述和灰点，不按数值区间错误归类为太阳或普通雨。
断网恢复、任务创建失败与 HTTP 初始化失败继续释放资源并允许重试。

## 绘制与布局

`weather_ui.c` 的 `CONDITIONS` 是 29 个 WMO 天气码及双语名称的唯一映射表。
0/1/2 的夜间分别使用晴夜/少云夜/多云夜图标；阵雨和阵雪在夜间以月亮替换背景太阳。
雾与冻雾、雨与冻雨、雪花与雪粒、雷暴与冰雹分别使用不同视觉元素；强度以数量/长度区分。
接口语义核对来源：[Open-Meteo WMO 文档](https://open-meteo.com/en/docs#weather-code)。

图标只有一个 `LV_EVENT_DRAW_MAIN` 绘制控件，温度也只有一个；点数不改变对象数。
使用 LVGL 原生圆角矩形绘制圆点和抗锯齿边缘，复用同一个图层，不引入位图依赖或动画定时器。
原生绘制能力参考 [LVGL 9.5 Draw API](https://lvgl.io/docs/open/9.5/main-modules/draw/draw_api)。
天气云以 28 列×12 行、9px 点距的共同轮廓表示，只有顶部云瓣间的外部凹口，没有内部孔洞。
阴天使用完整尺寸；带降水时缩小云形，为雨滴/雪花/闪电保留相同的总图标区域。

| 元素 | 466×466 原生坐标 / 尺寸 |
| --- | --- |
| 顶栏城市 | 顶端 y=56，20px 平滑字体 |
| 系统返回按钮 | 中心 (133,72)，40×40；退出恢复 48×48 |
| 城市标记 | 18×22，顶端 y=62，中心轴右移 66 |
| 天气图标 | 280×160，顶端 y=92，居中 |
| 温度 | 300×80，顶端 y=249；通常点距 11、点径 7 |
| 天气描述 | 顶端 y=335，20px；错误在同一区域显示 |
| 低高温 | 顶端 y=371，20px；箭头灰、数值暖白 |
| 湿度 | 顶端 y=410，16px 次要色 |

`font_weather_16.c` / `font_weather_20.c` 由 `tools/gen_weather_fonts.swift` 生成：
本地 CoreText 使用 Helvetica Neue / PingFang SC，将 ASCII、度符号、温度箭头和所需中文
转换为 4bpp 字形。只收集字符串，不把注释字集带入固件。两套字库共 139 个字符/套，
位图合计 48,160 字节。修改文案后运行 `swift tools/gen_weather_fonts.swift` 并重新验证缺字。

## 验证与防复发

`tests/weather/weather_ui_tests.c` 编译真实 `app_weather.c` / `weather_ui.c` / 字库，替换
HTTP、Wi-Fi 和 FreeRTOS，不访问真实网络。78 个 RGB565 实际页面渲染覆盖：

- 29 个天气码和三种夜间图标的中英文页面；仅比较图标区域像素，确保不同天气不只是换标签。
- 字形存在且不是占位框、可见文字四角在圆屏安全区域内、点阵对象数恒定。
- `current_units` 不干扰数值解析，`is_day=0/1` 正确选夜间/日间图标，表盘缓存接口保持有效。
- 负温度、三位温度、未知码、加载、断网、重进、恢复、单任务保护与任务/HTTP 初始化失败。

经验：仅按 `WX_OK` 状态去重会漏掉两次 UI tick 之间完成的“成功→加载→成功”。
本轮真实调用链回归已暴露此问题，`s_revision` 修复并由连续快速请求用例加固；
下次检查缓存刷新、昼夜切换和状态去重时，应同时检查数据版本，不假设一定看到中间加载态。

```sh
cmake -S GeekTool-IDF/tests/host -B /tmp/geektool-weather-host
cmake --build /tmp/geektool-weather-host -j 8
ctest --test-dir /tmp/geektool-weather-host --output-on-failure
# 可选：已有目录接收 466×466 原始 framebuffer，用于真实渲染对照。
/tmp/geektool-weather-host/weather_ui_tests /tmp/geektool-weather-renders
idf.py -C GeekTool-IDF build
```

主机画面重建系统顶栏，电量设为示例值 6%；生产电量由 `power` 服务驱动。
当前未烧录、未验收设备颜色/亮度、触摸返回、下拉/锁屏遮挡和真实网络昼夜切换。
本天气任务保留工作区修改，未提交或发布。并行 OTA 对话提出统一处理版本，
实际提交/发布应遵循该对话中的用户授权及最终代码验证。
