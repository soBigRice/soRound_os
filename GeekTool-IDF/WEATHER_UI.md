# 天气页：彩色 Nothing 风格 / 466×466 AMOLED

核对日期：2026-10-04。用户已确认彩色效果稿并授权实现；代码、主机渲染和固件构建的
验证见下文，设备视觉仍待验收。效果稿不是原生像素设计文件，图标按整屏比例转换为
466×466 的 RGB565 基准；逐像素验证针对该基准与真实 LVGL framebuffer，不代表真机验收。

## 目标与边界

- 保留已确认的城市 → 天气图标 → 点阵温度 → 天气描述 → 低高温 → 湿度层级。
- 点阵是图形风格，AMOLED 使用灰蓝云层、暖琥珀太阳、雾蓝降水、冰蓝雪与淡金闪电。
  正文使用局部平滑字体，不替换其他 App 的既有字体。
- 城市/红色标记可点击选择省市区，默认上海；地点与表盘缓存共用。详细链路见
  [设置与地址模块](./SETTINGS_LOCATION_DESIGN.md)。不申请定位权限。
- 数据接口、Wi-Fi 单任务保护、表盘缓存 API、成功后 20 分钟/失败后 1 分钟后台轮询保持原契约。
  current 的 `is_day` 决定昼夜，地点切换使旧缓存失效；月亮不依赖固定时段。
- 电量环仍表达真实电量、低电与充电状态；天气页仅调整粗细与背景，退出后恢复。
  保留并行 OTA 工作的隐藏电量层、点阵返回键和导航逻辑。

## 入口与状态流

`launcher.c:enter_app` → `header_app_style` 应用天气页局部顶栏样式 →
`app_weather.c:weather_enter` → `weather_details_create` / `weather_ui_create(hero)` → `start_fetch`。

2026-10-04 首屏像素保持，扩展为原生垂直滚动视口，下方增加当前详情、12 小时趋势、五天预报、日光/UV 和右侧圆弧滚动条。请求现在通过 `weather_data_parse` 使用项目已有 cJSON 按对象解析扩展字段；表盘缓存及地点/轮询契约保持。完整链路与本轮验证见 [天气详情说明](./WEATHER_DETAILS_DESIGN.md)，下文首屏布局及原图基准仍适用。

`wx_task` 保持独立 HTTP 任务：读取 current/daily，解析 `temperature_2m`、湿度、天气码、
`is_day` 和当日低高温 → 在请求 generation 仍等于当前地点时写入缓存 → 增加 `s_revision` → 发布 `WX_OK`。
缓存通过 `portMUX` 同步，旧请求晚返回丢弃，不改变新地点的加载状态。
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
`icon_event` → `weather_artwork_for` → `lv_draw_image` 绘制已确认原稿资源。
`artwork/weather/reference` 保留三张原稿，`manifest.json` 记录源文件 SHA256、裁切框、
原生坐标和解压尺寸；`native` 是固定 RGB565 图标基准，不凭轮廓猜测点数、点距或颜色。

`tools/gen_weather_artwork.py` 按整屏 528→466 的比例只缩放一次。晴天、阴天、小雨采用
主页面的原始位置；其他图标从原图册裁去文字后按同一比例转换，并居中放入 280×160 区域。
背景处理只去掉原稿的孤立暗噪点：亮度种子及 2px 边缘保留原始点阵像素，不重绘或移动圆点。
资源使用设备原生 RGB565 和 LVGL 无损 RLE，共 37 个图标、784,978 字节，最大临时解码
54,468 字节。必须同时启用 `LV_USE_RLE` 与 `LV_BIN_DECODER_RAM_LOAD`；仅启用 RLE
会导致解码器拒绝压缩图片、图标空白。现有 PSRAM malloc 配置支持解码内存，未增加缓存或定时器。
编码契约参考 [LVGL RLE 文档](https://lvgl.io/docs/open/libs/image_support/rle)。

原稿提供 0/1/2 三种夜间图标。实拍 IMG_3069 的小阵雨（80，夜间）曾落入旧程序化
云形，横纵点距 .84/.73 不一致，显得扁。现将原稿 2n 的月亮/云层与各原稿降水条组合，
补齐 80/81/82/85/86 五种夜间降水；不拉伸云层、不猜点阵。新增组合位于 280×160
区域内，清理重采样的暗色边缘，保留所有亮点。它们是原稿派生状态，不称为不存在的原图
的逐像素还原；其固定基准与原生绘制仍逐像素比较。未知码/无数据继续显示灰点。

| 元素 | 466×466 原生坐标 / 尺寸 |
| --- | --- |
| 顶栏城市 | 顶端 y=52，24px 地点字库，166px 宽超长省略 |
| 系统返回按钮 | 中心 (133,72)，40×40；退出恢复 48×48 |
| 城市标记 | 18×22，顶端 y=62，中心轴右移 66 |
| 天气图标 | 280×160，顶端 y=92，居中 |
| 温度 | 300×80，顶端 y=249；通常点距 11、点径 7 |
| 天气描述 | 顶端 y=335，20px；错误在同一区域显示 |
| 低高温 | 顶端 y=371，20px；箭头灰、数值暖白 |
| 湿度 | 顶端 y=410，16px 次要色 |

`font_weather_16.c` / `font_weather_20.c` 由 `tools/gen_weather_fonts.swift` 生成：
本地 CoreText 使用 Helvetica Neue / PingFang SC，将 ASCII、度符号、温度箭头和所需中文
转换为 4bpp 字形。只收集字符串，不把注释字集带入固件。16px 新增最近地址所需省名，共 185 个字符；20px 保持 139 个字符，
原位图合计 57,268 字节；2026-10-04 发版容量修正只裁去透明空边，保留 4bpp alpha/基线/advance，位图缩至 28,088 字节。全部 324 字形以固定语义哈希验证，首屏像素基线不变。修改文案后运行 `swift tools/gen_weather_fonts.swift` 并重新验证缺字。

## 验证与防复发

`tests/weather/weather_ui_tests.c` 编译真实 `app_weather.c` / `weather_ui.c` / 字库，替换
HTTP、Wi-Fi 和 FreeRTOS，不访问真实网络。108 个 RGB565 实际页面渲染覆盖（详情见天气详情说明）：

- 29 个天气码和八种夜间图标的中英文页面；仅比较图标区域像素，确保不同天气不只是换标签。
- 字形存在且不是占位框、可见文字四角在圆屏安全区域内、点阵对象数恒定。
- `current_units` 不干扰数值解析，`is_day=0/1` 正确选夜间/日间图标，表盘缓存接口保持有效。
- 负温度、三位温度、未知码、加载、断网、重进、恢复、单任务保护与任务/HTTP 初始化失败。

`tools/check_weather_artwork.py` 将 32 个原稿基准及五个派生夜间基准与中英文真实 LVGL 图标 framebuffer 比较，
74 组像素完全一致。原实现的阴天图标与该基准有 16,354 个像素不同，可识别此前近似轮廓问题。
2026-10-04 LVGL 9.5 与上游 9.6.0 各十一组 host 回归和固件构建通过；包含原首屏、
详情与透明字形裁剪保护。已发布 `v1.7-beta.14`：CI 镜像 `0x2fdda0` 字节，
3 MiB OTA 分区剩余 `0x2260`（8,800）字节，R2/国内 beta 对象与 GitHub 资产一致。
本轮未进行 USB 写入，设备观感、实际地址请求和触摸滚动仍待验收；
发布证据见 [PORTING_NOTES](./PORTING_NOTES.md#2026-10-04-v17-beta14-发布核对)。

经验：用户要求“一比一”时，应先找到其确认的原稿，固定整屏比例、点数与坐标；
近似的程序化云形不能作为验收基线。以后变更图标必须同时检查原稿、原生基准及真实渲染，
不能只检查不同天气图标互不相同。

经验：仅按 `WX_OK` 状态去重会漏掉两次 UI tick 之间完成的“成功→加载→成功”。
本轮真实调用链回归已暴露此问题，`s_revision` 修复并由连续快速请求用例加固；
下次检查缓存刷新、昼夜切换和状态去重时，应同时检查数据版本，不假设一定看到中间加载态。

```sh
cmake -S GeekTool-IDF/tests/host -B /tmp/geektool-weather-host
cmake --build /tmp/geektool-weather-host -j 8
ctest --test-dir /tmp/geektool-weather-host --output-on-failure
# 可选：已有目录接收 466×466 原始 framebuffer，用于真实渲染对照。
/tmp/geektool-weather-host/weather_ui_tests /tmp/geektool-weather-renders
python3 GeekTool-IDF/tools/check_weather_artwork.py /tmp/geektool-weather-renders
idf.py -C GeekTool-IDF build
```

host 像素回归及重新生成图标需要 Python 3 与 Pillow；生成运行
`python3 GeekTool-IDF/tools/gen_weather_artwork.py`。
未经用户确认不替换 `reference` 或重新设计点阵。此次用户授权的地址/设置功能
另外维护于 `SETTINGS_LOCATION_DESIGN.md`，既有温度/正文布局不随图标改变。

以下为此前安装记录（不是本轮镜像的设备证明）：

主机画面重建系统顶栏，电量设为示例值 6%；生产电量由 `power` 服务驱动。
`v1.7-beta.11` 发布镜像已于 2026-10-03 USB 烧录并确认启动；设备颜色/亮度、触摸返回、
下拉/锁屏遮挡和真实网络昼夜切换仍待验收。随后本次图标修正版已安装到 `ota_1`，
版本 `v1.7-beta.11-1-g4371a46-dirty`，启动/显示/触摸初始化与 Wi-Fi 重连正常，
原 `ota_0` beta.11 保留；具体校验见下方联合发布记录中的“天气原稿图标修正版 USB 安装”。
用户在 OTA 对话授权统一提交和发布；本页已随 `v1.7-beta.11` 与 OTA 改版合并发布。
代码提交为 `d31d2c8`，发布资产与通道核对见
[联合发布记录](./PORTING_NOTES.md#2026-10-03-v17-beta11-发布核对)。

复发检查：图册覆盖不等于状态覆盖，日间资源正确时还应核对实拍的具体 WMO code / is_day
是否落入备用绘制分支。本轮固定夜间 80/81/82/85/86 的真实 framebuffer 回归，避免再遗漏。

容量经验：本地与 CI 依赖的体积不同，发版前必须核对 CI 镜像是否适配实际 OTA 槽。字体体积问题先检查已导出位图的透明边界；无损裁剪需要同步补偿基线偏移，并对全部字形、字号及整屏像素加固，不能降低抗锯齿或扩大分区换取表面通过。
