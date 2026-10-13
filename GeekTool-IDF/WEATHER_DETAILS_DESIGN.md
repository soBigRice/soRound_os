# 天气：保留首屏、向下浏览详情

核对日期：2026-10-13。保留首屏天气内容和原生五屏惯性浏览。用户为流畅度明确批准去掉天气圆弧：电量用百分比、页位置用5个小刻度、日光轨迹改为直条。weather7已安装，用户确认“好多了，都正常的”，包括天气跟手感、百分比/刻度和返回桌面的电量环恢复。32MiB分区迁移已完成；软件/设备证据见下文与PORTING_NOTES，尚未测试真正网络OTA。

## 入口与布局

`launcher.enter_app → app_weather.weather_enter → weather_details_create → weather_ui_create(hero)`。

固定的 `s_content` 包含滚动视口、5刻度指示器和原有城市点击区。视口宽高仍为 466×466，首屏原有排版与图标资源不改，首屏字库的字形、字号、排版和抗锯齿保持；发版时仅无损裁去字库透明空边。原有城市标题、返回键与天气电量百分比位于启动器顶层；滚动后视口绘制黑色顶部遮罩，避免文字穿过标题。首屏静止时不绘制遮罩或滚动条。

向上滑动内容即可浏览首屏下方的四个 466px 绘制区；向下滑动返回顶部。使用 LVGL 原生垂直惯性滚动，关闭弹性拉伸及父级滚动传递，不强制整页吸附。

| 区域 | 实际信息与呈现 |
| --- | --- |
| 此刻详情 | 体感、降水、风速、阵风、云量、湿度；海平面气压、能见度、风的来向及数据时间 |
| 未来 12 小时 | 包含当前整点的 12 个小时温度趋势、逐小时降水概率、时段最高概率及累计雨量 |
| 未来五天 | 日期、原有天气图标、低高温及其范围、每日最高降水概率和累计雨量 |
| 日光与 UV | 当日最高 UV、日出日落、日照时长；依据地点的更新时间在日光轨迹中标记位置 |

详情采用四个固定绘制对象，通过 `lv_draw_label/rect/line/image` 绘制。进入视口时，文字上移最多 12px 并渐入，温度曲线和日温范围随可见比例展开。没有持续运行的装饰计时器、每帧对象创建或整个页面的透明离屏层。五日小图标复用原有 RGB565/RLE 素材，只在详情内缩放。

右侧5个小矩形刻度按实际滚动比例高亮最近页；滚动结束650ms后以220ms淡出，只使刻度的小区域失效。地址选择、快捷面板遮挡与退出均停止惯性和淡出动画，退出清空引用。

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


## 2026-10-13 传输流水线与图片缓存候选

状态：`v1.7-beta.33-weather2` 已按ESP-IDF6.0.1、LVGL9.6.0~1、esp_lvgl_port2.9.0构建；未安装设备，真实滚动、TE脉冲、帧时间及功耗待验证。首屏、五屏数据、惯性、揭示、圆弧滚动条、RGB565字节序、80MHz QSPI和20ms刷新周期保持。

原`weather_refresh_flush`在完整PSRAM帧生成后，只借用最后一块LVGL缓冲，逐块复制→发送→等待，另一块内部DMA缓冲没有参与搬运。现在从公开`lv_display_get_buf_active`记录两块固定缓冲；只有最后一个绘制块完成后才借用它们。复制下一块时上一块DMA可继续读另一块；`wait_send`返回后才提交下一块，最后一次完成前不调用`lv_display_flush_ready`。没有增加内部DMA内存或后台传输任务。单缓冲先等待再复制；没有`wait_send`的同步发送接口保留。其他App的原异步部分刷新路径保持。

`weather_frame_patch/read`仅在全宽、连续行条件下合并为一次memcpy，带padding或局部矩形仍逐行复制。TE仍每个完整帧等待一次，不改变既有撕裂保护。`display.c:weather_timing_event`通过REFR_START/REFR_READY记录真正有绘制的周期，退出/遮挡天气时只输出一次绘制+传输平均/最大耗时、TE平均等待与超时数；空刷新不计入，没有常驻采样任务。这不是输入到显示延迟，也不能直接用平均值当FPS。

另一处可证实的重复工作：`CONFIG_LV_CACHE_DEF_SIZE=0`使同一压缩天气图标每次打开都重新走解码。37张天气图解码RGB565合计约1.54MiB，单张最大54,468B。本轮使用LVGL原生1MiB有界缓存，按需保留常用图片、满时淘汰；不预先填满PSRAM，不改变原件/分辨率/颜色。大块像素由普通malloc及1KiB优先级进入PSRAM，少量缓存元数据仍可能在内部RAM。缓存可跨页面保留，生命周期交给LVGL，须在预热后判断泄漏。

验证：`weather_refresh_tests`模拟延迟DMA完成，校验在途缓冲内容、交替使用两个缓冲、另一块复制与在途DMA重叠、单缓冲安全回退、最终像素、脏区、一次TE与无刷新场景。`weather_cache_tests`用真正RLE解码器/40行部分刷新，比较同一图标24帧移动：解码器打开120次→1次，逐帧完整像素memcmp一致；LVGL9.5/9.6均通过。测试观察内部解码器结构，按两版本实际src/draw与src/image路径选择；产品仅使用公开显示API和标准缓存配置。

LVGL9.5全39项主机回归通过；9.6天气布局/条件、完整帧/TE和图片缓存3项通过。9.6完整主机集被既有流体等模块的旧flag API弃用告警阻塞，本轮没有关闭告警、削弱断言或顺手迁移无关UI。发布版本完整固件已构建成功。继承9.5 sdkconfig时，9.6弃用的ASSERT_HANDLER_INCLUDE在隔离配置中改为支持的ASSERT_USE_CUSTOM_INCLUDE/ASSERT_CUSTOM_INCLUDE，保留原assert.h，不修改断言行为或告警策略。

采用内部DMA缓冲的稳定路径；没有直接切PSRAM DMA。官方问题[IDFGH-17866](https://github.com/espressif/esp-idf/issues/18764)报告5.5.4中相近CO5300/QSPI+Wi-Fi场景的PSRAM直传下溢，它是兼容性线索，不能当作本机6.0.1已复现的缺陷。公开SPI传输完成回调约束见[ESP-IDF6.0.1](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/api-reference/peripherals/lcd/spi_lcd.html)；缓存实现核对实际LVGL9.5和发布9.6源码。

早期发布组件候选在被忽略的`build/weather-opt/`，由提交3c0149a加本轮修改构建，排除并行木鱼图标改动。2026-10-13实际设备备份核对后，以正在运行的本地组件和木鱼图标重建`v1.7-beta.33-weather3-8m`，候选/ELF/源哈希在`build/flash-records/partition-8m-20261013/`，39项主机检查有效。这样保留已部署效果；并行图标文件不属于本轮提交。双8MiB布局已获迁移授权并更新源码，但USB连续读回失败，未刷写；实际设备仍为原布局/原固件，详见[容量与迁移记录](./PORTING_NOTES.md#2026-10-13-天气优化与32mib容量规划)。

恢复迁移时先核对候选SHA/ELF与实际设备最新备份，再在同一USB/网络/亮度/表盘条件下测试真实下拉、惯性、其他页面、音频及OTA。TE超时占比高时先核实GPIO13真实脉冲/接线，不凭增加延时或关掉TE掩盖问题。未测得真机FPS、空闲堆增量或用户主观验收，未本地提交或发布。


2026-10-13实机补充：已按实际设备LVGL9.5.0/esp_lvgl_port2.8.0~1基线安装weather3-8m，并完成8MiB双槽与15.875MiB文件区迁移。用户反馈拖动仍很卡，流畅度未通过。231个绘制帧平均绘制加传输190416us、峰值271067us，TE平均7835us且0超时；不能以主机像素/缓存测试或RAM余量证明实际帧率。weather4-prof在`display.c/weather_timing_event/synchronized_flush/send_frame_tile/wait_frame_tile`累计总帧、flush、提交、DMA等待和字节数，仅退出汇总；绘制时间为总帧减flush，复制/函数开销含在flush减TE/提交/等待中。先定位阶段再决定优化，保留全部视觉/惯性/揭示动画约束。迁移与恢复证据以[容量规划记录](./PORTING_NOTES.md#2026-10-13-天气优化与32mib容量规划)为准。


weather4-prof真实拖动101个绘制帧：总平均123317us，绘制99837us，flush23480us（其中TE8598us、提交986us、DMA等待3129us），645次传输共22832448字节。另一短入页3帧绘制56333us。绘制占主要时间；与weather3拖动条件不同，不据此计算优化提升。

weather5候选仅在`weather_draw.h/weather_draw_visible`按公共`lv_layer_t.buf_area`过滤完全位于当前40行缓冲外的绘制。`weather_ui.c/dot/icon_event`与`weather_details.c/text/line/rect/arc/mini_icon`在申请LVGL任务/复制本地文字前先判定；可见任务沿用原坐标/颜色/透明度/缩放及LVGL最终裁剪，不截断图形。线/弧使用保守扩展范围，缩放小图预留2px边缘；不读取port私有context或改LVGL依赖。每帧重复调用custom draw时，LVGL9.5会在最终裁剪前申请fill/label/image任务，并为`text_local`复制字符串，原实现也处理了当前块外内容。

同LVGL9.5、40行缓冲与同输入：原绘制和weather5全部322张输出逐字节相同，包括120帧原生滚动动画。最初拿全屏缓冲作为对照时，部分运动帧的缩放图标有少量颜色差异；把未经修改的原绘制也设为40行后得到同样差异，属于原LVGL跨缓冲栅格差别，不能更新原基线来掩盖新回归。`weather_ui_tests`改为设备相同的40行缓冲，既有20帧首屏、74幅图标和324个字形保护仍通过；天气布局/惯性生命周期、DMA帧流水线、解码缓存共4项目标回归通过。weather5 ESP-IDF6.0.1固件构建通过、4162080B，真机提升待安装后测量和用户验收。


## 2026-10-13 用户批准去除天气圆弧

最新约束替代此前“右侧必须圆弧”的视觉要求：用户明确允许精简设计、去除圆弧以提升跟手感。weather5的块外任务过滤没有达到目标，用户确认仍卡，已安全撤销该过滤及`weather_draw.h`；没有把无效尝试叠在后续实现中。临时整帧PSRAM直接绘制原型在用户转向去圆弧时撤回，未安装设备、未扩大绘制缓冲、未改变其他App显示驱动。

隔离weather6-prof使用LVGL9.5官方profiling hooks，固定128统计项/64级栈，只在天气页启用、退出汇总，不常驻采样任务。44个绘制帧/0溢出：对象重绘8581次，`lv_obj_redraw`独占41708us/帧；`execute_drawing`独占36404us/帧，`lv_draw_sw_blend`32496us/帧。`lv_draw_arc`1734次、含子调用约60735us/帧；这些含子调用时间不能相加作为总耗时。天气仍平均177240us，绘制145681us。官方钩子源码仅在隔离诊断构建，正式weather7恢复关闭profiler，诊断代码不进入产品源码。

weather7按该批准取舍实现：

- `launcher.c/build_overlay/battery_visibility/battery_timer_cb/header_app_style`：天气使用顶部小字号实时电量百分比，充电图标移至其左侧，保持充电/充满/低电颜色与状态；天气隐藏大电量圆环。遮挡/锁屏隐藏相关电量层，离开天气恢复其他App既有电量环及图标位置。只新增一个小label，没有新增后台任务。
- `weather_details.c/indicator_draw/indicator_invalidate`：原大半径弧形滚动条改成右侧5个小矩形刻度，最近页高亮，保留原650ms延迟/220ms淡出；局部失效区缩至刻度范围。`daylight_draw`将日光半圆改为直线进度条，保留日出/日落、UV、日照时长与所选地点的真实数据。
- 五屏内容、12小时/5天数据、图标、温度、原生惯性、揭示动效及数据轮询周期保持。`display.c`仍使用原40行DMA缓冲与PSRAM帧合成，不直接向SPI提交PSRAM绘制内存。

11项目标检查通过：默认天气UI及实际RGB565_SWAPPED/40行路径、天气绘制不产生ARC任务、布局/字形/惯性/淡出/遮挡恢复、DMA/缓存，以及6组真实启动流/失败回滚门禁。已有74个图标和324个字形保持；归档20个全屏首屏hash包含旧装饰电量环，因此仅内容保护工具使用显式`--legacy-header`夹具核对这些旧hash，不更新旧hash掩盖布局变化，也不声称新头部全屏像素与旧截图一致。默认测试和当前渲染预览使用新的无圆弧头部。已查看当前首屏/日光页原生渲染预览；固件构建通过，4161824B、SHA256 `0e5e97411f4cee128c3dc6b7fbad9c836922bbdba9c7d3b3786e9ed3d801636b`，帧时间和主观体验待真机测量。


最终验收与收束：weather7设备日志122个绘制帧平均115288us、绘制85363us、flush29925us，TE平均8403us且0超时；拖动条件未标准化，不据不同版本日志计算提升百分比。用户确认“好多了，都正常的”。退出天气后系统页入口内部可用102483B、PSRAM可用8189668B（缓存保留约194KiB）；没有把原始不一致读数当作对照基线。最终USB只读审计确认seq29/30均VALID，新app/分区表/FAT与已校验镜像一致，原boot/PHY/两份旧4MiB区域仍相同，NVS在该审计中未变；随后恢复应用运行并关闭串口。完整恢复备份、最终候选/ELF、官方stub及验证摘要保留私有；临时源码/测试构建/像素导出和中间诊断候选清理。网络OTA下载/重启仍是未执行的独立验收，不等同于USB选槽通过。
