# 天气：保留首屏、向下浏览详情

核对日期：2026-10-04。用户要求首屏保持原样，在其下方增加天气信息、滚动动画与右侧圆弧滚动条。已实现原生 LVGL 界面；主机渲染和构建验证见下文，真机触摸与帧率待验收。用户随后授权提交与内测发版，发布结果见 PORTING_NOTES；OTA 分区不改。

## 入口与布局

`launcher.enter_app → app_weather.weather_enter → weather_details_create → weather_ui_create(hero)`。

固定的 `s_content` 包含滚动视口、圆弧指示器和原有城市点击区。视口宽高仍为 466×466，首屏原有排版与图标资源不改，首屏字库的字形、字号、排版和抗锯齿保持；发版时仅无损裁去字库透明空边。原有城市标题、返回键、电量环仍在启动器顶层；滚动后视口绘制黑色顶部遮罩，避免文字穿过标题。首屏静止时不绘制遮罩或滚动条。

向上滑动内容即可浏览首屏下方的四个 466px 绘制区；向下滑动返回顶部。使用 LVGL 原生垂直惯性滚动，关闭弹性拉伸及父级滚动传递，不强制整页吸附。

| 区域 | 实际信息与呈现 |
| --- | --- |
| 此刻详情 | 体感、降水、风速、阵风、云量、湿度；海平面气压、能见度、风的来向及数据时间 |
| 未来 12 小时 | 包含当前整点的 12 个小时温度趋势、逐小时降水概率、时段最高概率及累计雨量 |
| 未来五天 | 日期、原有天气图标、低高温及其范围、每日最高降水概率和累计雨量 |
| 日光与 UV | 当日最高 UV、日出日落、日照时长；依据地点的更新时间在日光轨迹中标记位置 |

详情采用四个固定绘制对象，通过 `lv_draw_label/rect/line/image/arc` 绘制。进入视口时，文字上移最多 12px 并渐入，温度曲线和日温范围随可见比例展开。没有持续运行的装饰计时器、每帧对象创建或整个页面的透明离屏层。五日小图标复用原有 RGB565/RLE 素材，只在详情内缩放。

滚动条只占右侧半径 214px 的 104° 圆弧，轨道 2px、滑块 3px。滑块长度根据视口/内容比例计算，位置根据实际滚动距离计算。使用原生圆弧绘制保证边缘平滑；滚动结束 650ms 后以 220ms 淡出。淡出只使右侧区域失效。地址选择、快捷面板遮挡与退出均停止惯性和淡出动画，退出清空引用。

新增字库 `font_weather_details.c` 仅覆盖详情中文与中点，16/20px、2bpp 位图合计 8,411 字节，其他字符回退到原天气字体。生成入口为 `swift tools/gen_weather_details_fonts.swift`；不得重生成或替换首屏字库来解决详情缺字。

## 请求、解析与缓存

`weather_poll/start_fetch → wx_task → weather_data_url → network_http_get → weather_data_parse → generation 校验 → s_data/s_revision → weather_tick → weather_ui_show + weather_details_show`。

`weather_data.c` 使用项目已有 cJSON 1.7.19，按 `current/hourly/daily` 对象解析，避免同名的 `*_units` 或逐小时字段被当作当前数值。最大 12 小时、5 天；URL 和 HTTP 发送缓冲均为 1KiB、响应缓冲保持 8KiB，拒绝请求头读取失败、响应读取错误、截断、非 200 和无效核心数据。上海公开接口实查响应约 2.4KiB；后台任务在解析后释放 HTTP、JSON 和响应内存。

首屏核心字段缺失、越界或低温大于高温时请求失败。可选字段的缺失、`null`、非有限/越界数值保留为 `NAN`，UI 显示 `--`；缺小时/逐日数组显示不可用状态，不补造预报。时间以接口 `timezone=auto` 的地点本地时间显示；风速明确请求 `m/s`，能见度由 m 转 km。降水量为 mm、气压为海平面 hPa，UV 是当日预测最大值。小时降水表示该整点前一小时累计，日降水表示当日累计；不把这些预测当作设备传感器读数。

`s_data` 是 UI 与原有 `weather_cached(temp,lo,hi,code,hum)` 的单一快照。保留单 HTTP 任务、地点 generation 防迟到响应、成功 20 分钟/失败 1 分钟轮询及天气表盘缓存接口。换地址重置滚动位置和详情可用状态；旧地点响应不能写入新地点。

2026-10-07：`start_fetch` 采用 `wifi_service_ready`，要求关联及DHCP/IP就绪；请求失败且IP已丢失也进入 `WX_OFFLINE`。
IP恢复后 `weather_poll` 立即重试，不再等待失败刷新周期。IP仍就绪但请求、解析或任务创建失败进入 `WX_FAIL`；
两者保持原提示和点击重试。共享传输检查SDK完整正文，日志区分连接/头部/HTTP/读取/容量/超时，记录DNS/TCP/TLS及内存；
解析失败另记录字节数。新规则、旧代码恢复缺陷的复现与真实设备边界见[联网恢复逻辑](./NETWORKING.md)。

接口字段、时间和单位依据 [Open-Meteo 官方 Forecast 文档](https://open-meteo.com/en/docs)，2026-10-04 实查。滚动依据 [LVGL 9.5 官方滚动文档](https://lvgl.io/docs/open/9.5/common-widget-features/scrolling)，同时验证本地 9.5 与发布环境 9.6；旗标调用经过 `lvgl_compat.h` 的兼容入口。

## 验证与防线

2026-10-04 用户反馈最新固件在已联网时所有城市均显示“无网络 / 获取失败”。核对 beta.15 源码的实际 URL：上海完整 URL 559 字节，`GET … HTTP/1.1\r\n` 请求行 549 字节；ESP-IDF 6.0.1 默认 TX 缓冲为 512 字节，`http_client_prepare_first_line` 在发送前返回失败。根因是详情增加查询字段后未同步扩大发送缓冲，和 8KiB 响应容量无关。对照请求在电脑直连 HTTP/HTTPS 均返回 200，响应约 2.4KiB；设备未接串口，不能把电脑成功当作设备恢复。

采用项目已有 `esp_http_client_config_t.buffer_size_tx`，设为 URL 缓冲大小 1024；不删字段、不改变城市、详情或轮询频率。依据 [ESP-IDF 6.0.1 源码](https://github.com/espressif/esp-idf/blob/v6.0.1/components/esp_http_client/esp_http_client.c#L1718-L1766) 核对限制。`weather_ui_tests` 的 HTTP stub 现在模拟默认 512 字节和完整请求行长度：修改前实际 worker 回归在首个成功天气断言失败，修改后通过；覆盖 open 超时、header 失败、完整 JSON 后的 read 错误、HTTP 500、断网与恢复，并检查已连接失败不再显示断网文案。以后扩展查询时先核对请求行和 TX 缓冲，而不是只检查响应夹具大小。

同日用户明确确认 **beta.16 仍获取失败**。发送缓冲修复通过自动回归，但不能据此认定设备问题已完全解决。
再次按当前完整 URL、HTTP/1.1 和设备默认 User-Agent 请求上海公开数据：HTTP/HTTPS 均返回 200，
chunked 响应 2,373B；真实 `weather_data_parse` 对两份响应均成功，12 小时/5 天数据完整。
这排除了本次电脑对照的接口参数与响应容量问题，不证明设备网络、DNS或内存分配正常。
下一步读取设备 `weather` 的 `forecast open=… headers=… http=… bytes=… read=…` 及相邻网络错误，
按阶段区分连接、头部、响应、解析和内存问题。USB 日志确认启动版本 beta.16，
串口连接出现 `USB_UART_CHIP_RESET`；未取得完整失败请求记录。用户随后明确确认天气已正常。
这是重启后的设备恢复反馈，原失败根因仍未证实，不继续盲改缓冲或转为 HTTPS。
再次失败时优先收集上述请求日志；不能把 USB 重启后的恢复解释为分区扩容已修复天气。
分区与资源压缩的核对及已授权迁移见 [分区迁移](./PORTING_NOTES.md#2026-10-04-分区扩容与usb迁移)。

beta.16 的 LVGL 9.5 / 9.6 各十二组主机回归通过，原首屏 20 帧、74 组图标像素与全部原字形语义检查通过。ESP-IDF 6.0.1 本地构建通过，镜像 3,109,536 字节，3MiB OTA 槽余 36,192 字节。beta.16 发布构建及 GitHub/R2/国内完整与断点下载校验通过，发布包 3,142,768 字节；用户随后确认 USB 重启后天气已正常，原失败原因尚未确定，详见 [发布记录](./PORTING_NOTES.md#2026-10-04-v17-beta16-发布核对)。

- `tests/weather/first_screen.sha256.json` / `first_screen_lvgl96.sha256.json` 分别固定修改前 LVGL 9.5/9.6 的 20 个完整首屏 RGB565 渲染：中英加载、三种典型天气、夜间、负温、三位温、未知码、断网与恢复。两版本在返回箭头处原本存在栅格差异，因此必须按实际渲染版本比较。9.6 基准取自改动前 00:36 构建的 beta.13 主机测试可执行文件，SHA256 `d07f95fb40e19c221b913e65a2766c256953998ec1da07d34fbecc75e806b873`；其 20 帧与修改后同版本逐帧一致。`check_weather_artwork.py` 查询测试程序版本，同时检查首屏和既有 74 个图标像素比较，不能更新基线掩盖首屏变化。
- `forecast_fixture.json` 为 2026-10-04 上海 Open-Meteo 公开响应，地点时间 13:15，仅用于离线回归，运行固件始终联网请求所选地址。详情截图先切换到上海，避免把夹具天气标在另一城市下。
- `weather_ui_tests` 检查实际 HTTP worker、限定数组和缺失字段、双语绘制字形及圆屏边界、真实指针拖动后的惯性、滚动条淡出、遮挡停止、滚动后地址点击区及静止不重绘。默认 110 帧，包含新增的双语接口失败提示；`--motion` 另导出 120 帧原生逐页滚动动画。
- 用户此前指出文字穿透；新详情必须审查实际 draw task 的文字四角和完整圆形安全区，不能只审查 LVGL label 控件或矩形屏幕。该检查已发现并修复中点缺字和小时页底部文字越界，加入永久回归。停止/重置原生滚动会发出 `SCROLL_END` 并新建淡出；必须先停止滚动、再删除淡出，退出及地址切换回归断言动画已清空，避免失效引用或首屏迟发滚动条。
- 最终 LVGL 9.5/9.6.0 各十一组 host 回归通过，包含每版本 20 帧首屏和 74 组原图像素保护。ESP-IDF 6.0.1 构建通过；最终发布资产以本页下方 CI 记录为准。未扩大分区，后续新增功能需要重新核对发布环境容量。
- 最低设备验收：上下滑完整五屏，轻拖/快速松手，确认动画帧率与圆弧淡出；从详情切换地址、断网重试、打开/收起快捷面板及退出重进。主机惯性测试和动画导出不等于实机流畅性验收。

## 发布容量修正

2026-10-04 首次 main 预构建 `37183064155` 在发布环境生成 `0x304fa0` 字节，超出 3MiB OTA 槽 `0x4fa0`（20,384）字节；Release/R2 步骤未运行，没有发布超容镜像。本地与 CI 的依赖差异再次说明不能只看本地剩余容量。

检查原始天气 4bpp 字形发现固定栅格的透明空边占用 29,180 字节。只裁去已量化为零的像素，并补偿 `ofs_x/ofs_y`，保持所有非零 alpha、基线坐标、advance、Unicode 映射和行高。两份字库由 57,268 缩至 28,088 字节，没有降色深、改变图标、删除功能或扩大分区。`gen_weather_fonts.swift` 同步采用同样裁剪；`font_semantics.sha256.json` 固定裁剪前全部 324 个字形的可见像素/排版语义，像素检查工具永久核对。LVGL 9.5/9.6 各十一组回归再次通过，包含原首屏和详情。

修正后 main 预构建 `37184442540` 与标签发布 `37184982751` 均通过，CI 使用 ESP-IDF 6.0.1、LVGL `9.6.0~1`。`v1.7-beta.14`（源码 `9680430`）发布包为 `0x2fdda0`（3,136,928）字节，3 MiB OTA 槽剩余 `0x2260`（8,800）字节，SHA256 `99ac2dabb69876e3cea98ef9654d1524f8ceecec7a1a6adfb3539775586e8a49`。GitHub、R2 与国内 beta 镜像逐字节一致，完整/断点下载通过；设备流畅性仍待验收。完整证据见 [PORTING_NOTES](./PORTING_NOTES.md#2026-10-04-v17-beta14-发布核对)。

## 2026-10-05 完整帧与TE同步

用户反馈天气滚动有分层跳动。当前直接链路是两块内部DMA 40行缓冲，渲染一块即推屏；初始化虽发送 `0x35/0x00` 开启TE，但从未读取TE输入。这是候选原因，尚无该次设备刷新日志或帧率测量，不能宣称已测得根因或提升百分比。用户批准仅天气页完整PSRAM帧+TE，其他App保留部分刷新。

`app_weather.enter/visibility/exit` → `display_weather_mode`在LVGL锁内申请/释放434,312字节（约424KiB）PSRAM，使能/停用GPIO13 TE中断。进入或重新显示时全屏失效以建立有效缓存；离开/遮挡释放，原天气首屏、五屏内容、惯性、揭示动画、弧形滚动条和数据频率不变。

`display.c/synchronized_flush` → `weather_refresh_flush` → `weather_frame_patch`按LVGL实际stride将所有脏块复制进RGB565_SWAPPED完整帧。非最后块立即flush_ready；最后块等待一次新的TE上升沿，再将本帧脏区域并集分批打包进当前最后一块LVGL DMA缓冲推屏。每块DMA完成后才覆写缓冲；整次推屏结束后再释放最后一次flush。保留未改变像素及面板偶数窗口对齐，不交换颜色字节、不增添常驻渲染任务、不额外申请整屏内部DMA内存。

TE最多等25ms，缺信号则日志标记并推送已合成帧；分配失败保持原刷新路径，不减分辨率或删除详情。自动light sleep期间GPIO边沿ISR不能唤醒，因此只在这次短暂TE等待内持有 `ESP_PM_NO_LIGHT_SLEEP`，退出等待即释放。其他App不启用TE中断。SPI完成回调按当前异步部分刷新/同步帧传输分别通知LVGL或DMA信号量。

方案使用现有LVGL9.5/9.6公共flush API与ESP-IDF6.0.1 SDK，不改依赖。`esp_lvgl_port 2.8.0~1` 的LVGL9实现未使用 `trans_size`，所以没有照抄README的PSRAM canvas示例依赖隐式bounce buffer；实际复用现有内部DMA缓冲。依据：[1.75C官方原理图](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C/blob/main/Schematic/ESP32-S3-Touch-AMOLED-1.75C-schematic.pdf)GPIO13/LCD_TE，[ESP-IDF LCD缓冲生命周期](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/api-reference/peripherals/lcd.html)、[esp_lvgl_port源码](https://github.com/espressif/esp-bsp/tree/master/components/esp_lvgl_port)。核查日期2026-10-05，最终行为以实际构建依赖为准：本地esp_lvgl_port `2.8.0~1`，beta.23 CI实际解析为 `2.9.0`；已定向核对官方2.9.0源码，SPI部分刷新、内部DMA分配、flush_ready完成语义及LVGL9未使用trans_size均保持一致。自定义刷新仅使用上述公共LVGL API，不读取port私有context。

`weather_refresh_tests`执行真实LVGL双缓冲/40行刷新，检查完整帧与面板字节一致、TE每帧只等一次、脏块并集、原字节序、DMA容量、静止不传与缓存缺失时原路径。原天气20帧首屏与74组图标基线仍须通过。软件验证不测量实际TE边沿、SPI传输帧率或屏幕撕裂；升级后轻拖/快速甩动五屏、遮挡恢复/退出再入、对比系统信息PSRAM占用与串口TE警告是最低真机验收。
