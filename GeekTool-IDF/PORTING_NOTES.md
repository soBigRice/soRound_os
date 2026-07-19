# GeekTool → ESP-IDF 移植配方与计划(无撕裂版)

目标:把 GeekTool 从 Arduino 迁到 **ESP-IDF + esp_lcd + esp_lvgl_port**,用硬件 DMA + 双缓冲
彻底解决"上半先动、下半延迟"的撕裂。配方全部来自小智(xiaozhi-esp32)官方对本板的支持。

> 参考:`78/xiaozhi-esp32` → `main/boards/waveshare/esp32-s3-touch-amoled-1.75/`
> 该板同时支持 `1.75` 和 `1.75C`,我们用 **1.75C** 的引脚。

---

## 0. 关键结论

- **无撕裂的来源**:`esp_lcd` 的 QSPI **硬件 DMA** + `esp_lvgl_port` **双缓冲**(整帧 DMA 推出、CPU 不阻塞)。
  面板 TE 在初始化里开了(命令 `0x35`),但**没有接 GPIO 做硬同步**——实测就已经够顺。
- **LVGL 版本会变成 9**(`esp_lvgl_port 2.7.x` 依赖 LVGL v9)。所以我们的 UI 代码要做 **8.4 → 9 的 API 移植**
  (`lv_disp_*`→`lv_display_*`、`transform_zoom`→`transform_scale`、事件取参 `lv_event_get_param` 等)。

---

## 1. 依赖组件(组件管理器自动拉取)

| 组件 | 版本 | 作用 |
|------|------|------|
| `espressif/esp_lcd_co5300` | `^2.0.3` | CO5300 QSPI AMOLED 面板驱动(自带 + 可覆盖 init) |
| `waveshare/esp_lcd_touch_cst9217` | `^1.0.3` | CST9217 触摸 |
| `espressif/esp_io_expander_tca9554` | `==2.0.0` | 板载 TCA9554 IO 扩展(地址 000) |
| `esp_lvgl_port` | `~2.7.2` | LVGL 移植层(双缓冲/DMA/刷新,LVGL v9) |
| `lvgl/lvgl` | `~9.2` | GUI |

---

## 2. 1.75C 引脚(⚠ 和 Arduino 版不同!)

```
显示 QSPI:  CS=12  PCLK=38  D0=4  D1=5  D2=6  D3=7
LCD 复位:   GPIO1        ← 1.75C 是 1(非 C 是 39);Arduino 版误用了 2
触摸:       RST=2  INT=11  (I2C 共用总线)
I2C:        SDA=15  SCL=14
音频 I2S:   MCLK=16  WS=45  BCLK=9  DIN=10(麦)  DOUT=8(扬)   PA=46
TCA9554:    I2C 地址 000(0x20)
BOOT 键:    GPIO0
屏:         466×466,无 mirror/swap,列偏移 gap=0x06
```

## 3. CO5300 厂商初始化序列(QSPI 模式,来自小智已验证)

```c
static const co5300_lcd_init_cmd_t vendor_specific_init[] = {
    {0xFE, (uint8_t[]){0x20}, 1, 0}, {0x19, (uint8_t[]){0x10}, 1, 0}, {0x1C, (uint8_t[]){0xA0}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0}, {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},   // 16bit/px
    {0x35, (uint8_t[]){0x00}, 1, 0},   // TE on
    {0x53, (uint8_t[]){0x20}, 1, 0}, {0x51, (uint8_t[]){0xFF}, 1, 0}, {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00,0x06,0x01,0xD7}, 4, 0},
    {0x2B, (uint8_t[]){0x00,0x00,0x01,0xD1}, 4, 600},
    {0x11, NULL, 0, 600},              // sleep out
    {0x29, NULL, 0, 0},               // display on
};
```
亮度:命令 `0x51`(1 字节,0~255)。偶数对齐:LVGL9 用 `LV_EVENT_INVALIDATE_AREA` 回调把刷新区域 x1/y1 向下取偶、x2/y2 向上取奇(CO5300 必须)。

显示创建关键点(`esp_lvgl_port`):双缓冲 + 缓冲放 PSRAM + 合适的 buffer 大小,
开 `full_refresh` 或 `direct_mode` 以获得整帧一致刷新(无台阶撕裂)。

---

## 4. 前置条件(你的电脑)

- 安装 **ESP-IDF v5.1+**(VS Code 的 Espressif 插件,或命令行 `idf.py`)。这是 Arduino 之外的另一套工具链。
- 第一次构建会自动从组件管理器下载上面的依赖。

---

## 5. 推荐的推进顺序(里程碑)

1. **(强烈建议先做)验证前提**:用 IDF 直接编译小智官方固件的 `esp32-s3-touch-amoled-1.75c` 目标,
   烧到你板子上,确认 IDF 这套**在你的硬件上确实不撕裂**。顺带把 IDF 环境跑通。
2. **M1 — 显示底层**:新建独立 IDF 工程,用上面的组件 + 引脚 + init,点亮屏 + 触摸 +
   一个 LVGL9 轮播 smoke test,确认**滑动无撕裂、无台阶**。← 风险都在这一步,先单独打通。
3. **M2 — UI 移植**:把 GeekTool 的启动器 + WiFi/I2C/System 三个 app 从 LVGL 8.4 移到 9,接到 M1 上。
4. **M3 — 收尾**:AXP2101 真实电量、省电、OTA 等。

## M1 结论(已验证、已定稿)

显示底层跑通并接受。最终配置:`esp_lcd_co5300`(QSPI **80MHz**)+ `esp_lvgl_port`
**单缓冲**(内部 DMA,160 行)+ `swap_bytes`。构建:`idf.py set-target esp32s3 && idf.py build flash monitor`。

**踩坑经验(都很关键)**:
- **SPI DMA 必须开**(`spi_bus_initialize` 用 `SPI_DMA_CH_AUTO`)。关掉 = 轮询阻塞 = 和 Arduino 一样撕裂,优势全无。
- LVGL 缓冲放**内部 DMA RAM**(`flags.buff_dma=1`),**别放 PSRAM**(`buff_spiram`+DMA 之前点不亮)。
- SPI/QSPI 屏用**单缓冲**(`double_buffer=false`);双缓冲部分刷新会**闪烁**(小智 SPI 屏也是单缓冲)。
- QSPI 时钟 `io_cfg.pclk_hz = 80MHz` 提吞吐;花屏就降 60/40。
- `esp_io_expander_tca9554` 在 IDF6 缺 `esp_driver_i2c` 依赖 → 已从依赖移除,M2 要用再找兼容版本(别改 managed_components)。

**残留撕裂的最终结论**:`esp_lvgl_port` 的 `avoid_tearing` 只支持 **RGB/DSI** 屏;CO5300 是 **QSPI**,
推屏不与屏幕刷新同步,**全屏快滑的撕裂是架构上限,代码不再深抠**(用户已接受)。

## M2 进度

**M2a — 启动器 + 导航(已烧录,启动正常)** `launcher.c`
- "小面积运动"设计:静止黑底 + 居中大图标,切换只动中心一小块,避开整屏滑的撕裂。
- 切换动画 `swap_exec`:中心图标+名字半程滑出淡出 → 中点换内容 → 反向滑入淡入。
  旋钮在文件顶部:`SWAP_MS`(总时长)、`SWAP_SLIDE`(滑动幅度,设 0 = 纯淡入淡出)。
- 连击保护:动画进行中 `lv_anim_get` 命中则忽略新手势。

**M2b-1 — 共享列表 + System + I2C(已随整机烧录启动,System/I2C 交互待实机逐项验证)**
- `ui_list.c`:圆屏曲率聚焦滚动列表,三个 app 共用。LVGL 8→9 改名:`lv_event_send`→
  `lv_obj_send_event`、`get_child_cnt`→`_count`、`clear_flag`→`remove_flag`、
  `LV_LABEL_LONG_DOT`→`..._MODE_DOTS`、回调里 `lv_event_get_target_obj`。
- `app_sys.c`:`ESP.*` → `esp_chip_info`/`esp_flash_get_size`/`esp_psram_get_size`/
  `esp_get_free_heap_size`/`esp_get_idf_version`;`millis()`→`esp_timer_get_time()/1000`。
- `app_i2c.c`:Arduino `Wire` → `i2c_master_probe()`;总线由 `main.c` 创建,经新增的
  `board_i2c_bus()`(声明在 `board_config.h`)共享给 app。

**M2b-2 — WiFi(已随整机烧录启动,自动重连已验证,配网交互待实机逐项验证)** `app_wifi.c`
- Arduino `WiFi` → `esp_wifi`:`wifi_svc_init()` 一次性建 netif/event loop/wifi 并 `start`;
  扫描 `esp_wifi_scan_start(async)`,连接 `esp_wifi_set_config`+`esp_wifi_connect`。
- **线程约定(关键)**:esp_wifi/IP 事件回调**只写 volatile 标志位,绝不碰 LVGL**;
  所有 UI 更新放在 `wifi_tick()`(LVGL 任务)里轮询标志 —— 沿用 Arduino 的轮询模型,
  免去给 LVGL 额外上锁。密码键盘对话框照搬到 LVGL 9(`lv_btn`→`lv_button`、
  `lv_obj_del_async`→`lv_obj_delete_async`)。去掉了 Arduino 版填充用的假网络。
- 依赖:`main/CMakeLists.txt` 加 `esp_wifi esp_netif esp_event`(及 M2b-1 的 `esp_timer` 等)。

**Flash**:16MB → **32MB**(`sdkconfig.defaults` 已改 `CONFIG_ESPTOOLPY_FLASHSIZE_32MB`)。
当前 `partitions.csv` 是双 OTA app 槽(`ota_0`/`ota_1` 各 3MB)+ 4MB `storage`;烧录会写入
`ota_data_initial.bin`,让新固件从 `ota_0`(`0x20000`) 启动。

## 2026-07-13 烧录验证记录

- 目标串口:`/dev/cu.usbmodem1101`。`esptool.py chip_id` 确认为 ESP32-S3 rev v0.2、8MB PSRAM、MAC
  `a4:cb:8f:d6:35:b8`;另一个 `/dev/cu.usbmodem309NTPCEG5682` 无串口响应,本次未使用。
- 环境:`export PATH="/opt/homebrew/bin:$PATH"` 后 `source ~/.espressif/v6.0.1/esp-idf/export.sh`;
  使用 ESP-IDF v6.0.1。
- 构建:`idf.py -p /dev/cu.usbmodem1101 build` 通过,生成 `build/GeekTool.bin`;
  镜像大小 `0x1a3fb0`,3MB OTA app 分区剩余 `0x15c050`(45%)。
- 烧录:`idf.py -p /dev/cu.usbmodem1101 flash` 写入 bootloader、partition table、`ota_data_initial.bin`,
  `storage.bin` 和 `GeekTool.bin`,所有段均 `Hash of data verified`,最后 hard reset 成功。
- 串口启动:`idf.py monitor` 需交互式 TTY;普通管道会报 `Monitor requires standard input to be attached to TTY`。
  使用 PTY 监视后确认从 `ota_0`(`0x20000`) 启动,Flash 32MB、PSRAM 8MB 正常识别。
- 启动日志已确认:`CO5300 panel ready`、`LVGL task`、`CST9217 touch ready`、`GeekTool M2a up`;
  WiFi 自动连接 `superRice`,获取 IP `192.168.2.109`。本次固件版本显示为 `d067acd-dirty`,
  对应烧录的是当前未提交工作区状态。
- 续烧录:当前工作区新增/修改了 `bootkey.*`、`app_fluid.c`、启动器和若干 app 文件后再次执行
  `idf.py -p /dev/cu.usbmodem1101 flash`;`GeekTool.bin` 大小 `0x1a48f0`,3MB OTA app 分区剩余
  `0x15b710`(45%)。bootloader、partition table、`ota_data_initial.bin`、`storage.bin` 和 `GeekTool.bin`
  全部 `Hash of data verified`;PTY monitor 再次确认屏幕、触摸、主应用和 WiFi 自动重连正常。
- 再次续烧录:当前工作区继续修改 `app_wifi.c`、`lock.c`、`main.c`、`sdkconfig.defaults` 等后执行
  `idf.py -p /dev/cu.usbmodem1101 flash`;由于 `sdkconfig.defaults` 变化触发完整重编译。`GeekTool.bin`
  大小 `0x1a79b0`,3MB OTA app 分区剩余 `0x158650`(45%)。所有写入段仍全部 `Hash of data verified`;
  PTY monitor 确认从 `ota_0` 启动,Flash 32MB、PSRAM 8MB、CO5300、CST9217、LVGL、主应用和 WiFi 自动重连正常,
  WiFi 获取 IP `192.168.2.109`;日志显示 Light sleep 已启用。
- 本轮续烧录:继续执行 `idf.py -p /dev/cu.usbmodem1101 flash`,仅增量重编译 `app_fluid.c` 等;
  `GeekTool.bin` 大小 `0x1a7d20`,3MB OTA app 分区剩余 `0x1582e0`(45%)。bootloader、partition table、
  `ota_data_initial.bin`、`storage.bin` 和 `GeekTool.bin` 全部 `Hash of data verified`;PTY monitor 确认从
  `ota_0` 启动,Flash 32MB、PSRAM 8MB、CO5300、CST9217、LVGL、主应用和 WiFi 自动重连正常,IP 仍为
  `192.168.2.109`,Light sleep 仍启用。串口还看到 `enter maze` / `QMI8658 ready` / `enter fluid` 日志,
  仅代表 app 可进入并初始化到该阶段,交互效果仍需肉眼验收。
- 未完成的实机操作验收:启动器左右滑/进入/返回、System/I2C 列表、WiFi 扫描与密码连接、天气请求、
  设置亮度/音量、锁屏/侧键/省电、OTA 真 URL、音频 app 的 `rms=` 随声音变化。

```mermaid
flowchart TD
    A["发现两个 USB modem 串口"] --> B["esptool chip_id 探测"]
    B --> C["确认 /dev/cu.usbmodem1101 是 ESP32-S3"]
    C --> D["idf.py build"]
    D --> E["生成 GeekTool.bin, OTA 分区剩余 45%"]
    E --> F["idf.py flash"]
    F --> G["bootloader / partition / ota_data / storage / app 全部 hash verified"]
    G --> H["PTY monitor 启动验证"]
    H --> I["屏幕 / 触摸 / WiFi / 主应用日志正常"]
    I --> J["进入实机交互逐项验收"]
```

## M3 进度

**M3a — AXP2101 真实电量 + 充/放电可视化(已随整机烧录启动,电量显示和充放电表现待肉眼验证)**
- `power.c`/`power.h`:挂 AXP2101(I2C `0x34`),只读不写。电量 `0xA4`,
  方向 `0x01[6:5]`(1=充/2=放),充满 `0x01[2:0]==100`(寄存器取自小智 `common/axp2101.cc`)。
- `launcher.c` `battery_timer_cb` 每 2s 读一次,更新电量环 + ⚡:
  - 环颜色编码状态:放电 = 绿/琥珀/红(按电量);充电 = 青蓝(`COL_WIFI`);充满 = 绿(`COL_OK`)。
  - ⚡(`LV_SYMBOL_CHARGE`,顶端居中)仅充电/充满时显示;**充电时呼吸**(透明度动画)、充满常亮。
    只动这一小块,守住"小面积运动"避撕裂原则。
- 电量环写死的 72 已去掉(初始 0,开机立即读一次)。
- 充电参数(CV 电压/充电电流)没碰 —— 要调照小智板级 init 写 `0x64/0x61/0x62/0x63`。

**M3b — 锁屏 / Nothing 表盘 / 侧键 / 省电(已随整机烧录启动,侧键和省电策略待实机操作验证)**
- `watchface.c`:全屏黑底点阵表盘,挂 `lv_layer_top`(盖住启动器+app)。手绘 5×7 点阵数字、
  60 点外环、红点冒号(1Hz 闪)、沿环走的秒点;数字每分钟才重建 → 低运动避撕裂。
  下方信息区:日期、WiFi 名、电量% + IP(连上 WiFi 后显示)。
- `lock.c`:**AXP2101 PWRON 侧键**(不是 BOOT)—— 短按=锁/解切换、长按=关机。
  去抖+长短判定由 PMU 硬件做,软件只轮询 IRQ(`power_key_event`:`0x49` bit3=短/bit2=长,写 1 清,
  使能在 `0x41`)。表盘**上滑解锁**。省电:**锁屏+放电**时空闲 15s 变暗(亮度 `0x20`)→
  30s 熄屏(`display_sleep`);**充电常显**;触摸/按键唤醒。
- `display.c` 新增 `display_set_brightness`(`0x51`)+ `display_sleep`(`disp_on_off`)。
- 时间:`localtime`(时区 `CST-8` 在 main 设),WiFi 连上后 `esp_sntp` 自动校时(`pool.ntp.org`)。
  **没接 PCF85063 RTC**,没联网时表盘从开机零点起走。
- PWRON 键寄存器取自 XPowersLib(`~/Documents/Arduino/libraries/XPowersLib`)。

**M3c — OTA(本段记录初版移植;当前发布链路见根 README 与本文件 2026-07-19 记录)**
- `partitions.csv`:改双 app 槽 `ota_0/ota_1`(各 3MB)+ `otadata`(原单 `factory`)。
  **改了分区表,下次 flash 会重新分区**;`nvs` 偏移不变(WiFi 配置等保留)。
- `app_ota.c`:点更新环 → 独立任务跑 `esp_https_ota`(带 crt bundle,支持 HTTPS),成功后
  `esp_restart`,状态在 `ota_tick` 显示。**需先连 WiFi**。当前按 NVS beta 开关选择 Cloudflare R2
  正式对象 `GeekTool.bin` 或内测对象 `GeekTool-beta.bin`,不再使用初版的本地占位 URL。
- 依赖加了 `esp_https_ota app_update esp_http_client esp-tls mbedtls`。

## 里程碑完成

M1 显示 → M2 启动器+WiFi/I2C/System → M3 电量/锁屏/Nothing 表盘/省电/OTA。
可选后续:点阵日期、充电常显防烧屏(降亮度)、OTA 进度条、PCF85063 离线走时。

## UI 统一 — Nothing 单色(已随整机烧录启动,视觉细节待肉眼验收)

全局改造靠两个集中开关,不逐控件改:
- **调色板**(`app.h`):全部收敛到 黑`COL_BG` / 白`COL_TXT` / 灰`COL_TXT2` / 一个红`COL_RED`。
  旧彩色别名(`COL_WIFI/I2C/SYS/OTA/OK/RING`)都 `#define` 成白、`COL_WARN`=红 → 改这几行就能全局变色,各 app 不用动。
- **主题**(`main.c`):`lv_theme_default_init(disp, 红, 白, dark=true, montserrat)` + `lv_display_set_theme`
  → 键盘/按钮/文本框等默认控件统一暗色红强调。`sdkconfig.defaults` 开了 `LV_USE_THEME_DEFAULT`。
- **字体**(`app.h` 三个宏):正文 `UI_FONT_L/M` = 内置点阵像素字 `unscii_16`;含图标(⚡ / ‹ › / 键盘符号)的
  标签用 `UI_FONT_SYM` = `montserrat_20`(unscii 没有图标字形,符号会变空格)。开了 `UNSCII_8/16`。
- 细节:启动器图标 → **描边圆环**(不填充);OTA → **红色 CTA 按钮**;电量环 放电=白 / 低电+充电=红;
  表盘布局没动(用户要求),只把日期/WiFi 标签换成同一像素字(电量行因带 ⚡ 仍用 montserrat)。

**换真 Ndot 字体**:现用内置 unscii(够 Nothing 味、零转换风险)。要上 Nothing 官方 **Ndot**:
用 lv_font_conv 把 Ndot.ttf 转 LVGL 字体(合并 `0xF000-0xF8FF` 符号区),丢进 `main/`,
`app.h` 把 `UI_FONT_L/M` 指过去即可 —— 字体已集中,改一处。

## App 点描图标 + 天气 app(已随整机烧录启动,天气请求和图标观感待实机操作验证)

- **点描图标**:通用画法在 `glyph.c`/`glyph.h`(`glyph_arc`/`glyph_line`/`glyph_circle` 沿轮廓匀距撒小圆点,
  比之前 9×9 大点精致)。启动器图标 `launcher.c` 的 `ic_wifi/ic_scan/ic_chip/ic_ota/ic_sun`
  (`ICON_FN[]` **顺序对齐 `APPS[]`**),细线轮廓 + 红点焦点。加新 app = `APPS[]` 和 `ICON_FN[]` 各加一行。
- **天气 app**(`app_weather.c`,第 5 个):**Open-Meteo**(`api.open-meteo.com`,**HTTP** 不用 HTTPS,无 key)拿
  当前温度 + WMO 天气码 + 湿度 + **当日低/高温**。响应 ~1-2KB。**不用 cJSON**(组件名解析不到的坑):
  十几行 `json_num`(strstr 找 `"key":` 取无引号数值,数组跳 `[`)解析,**先定位到 `current`/`daily` 段**
  再取值,避开前面 `*_units` 段同名键的字符串值。天气码→图标用 `draw_wicon(int code)` 按 WMO 区间选(`wmo_desc` 给文字)。
  云/晴/雨/雪图标都用 `glyph_*` 点描(云 = 重叠圆求外轮廓 + 底边线);温度 5×7 大点阵 + 度环。
  顶部标题 `launcher_set_title(CITY)` 显示城市(`icon_cb` 已改成先设标题再 `enter`,app 可覆盖)。
  布局 y 固定不重叠:云 87–192 / 温度 205–296 / 天气 312 / 低高+湿度 344 / 状态 376。需先连 WiFi,换城市改 `WX_LAT/WX_LON`。
- 坑回顾:① `glyph_dot` 漏了 `bg_opa`(remove_style_all 后默认透明)→ 点全看不见,补 `LV_OPA_COVER`。
  ② GCC `-Wformat-truncation` 对小缓冲 snprintf 误报 → `main/CMakeLists.txt` 加 `-Wno-format-truncation`。
  ③ **天气走 HTTP 不走 HTTPS**:S3 内部 RAM 被显存(单缓冲 160 行)+WiFi 占满,mbedtls `ssl_setup`
  分配不出 ~32KB 收发缓冲 → `-0x008D` 失败。公开天气数据没必要 TLS,Open-Meteo 裸 HTTP 直接给 JSON。
  ④ **天气图标用户不满意但决定不改了**(试过 9×9 实心 / 描边 / 重叠圆 / 网格点阵几版),维持现状。

## 设置 app(第 6 个,已随整机烧录启动,滑条交互待实机操作验证)

- `settings.c`/`settings.h`:全局亮度 + 音量状态,存 NVS(命名空间 `settings`)。
  - **亮度**:`settings_set_brightness` 直接驱动 CO5300(`display_set_brightness`),拖动实时、松手才写 NVS(防频繁写 flash)。
    开机 `settings_init()`(在 `main.c` `display_init()` 之后)从 NVS 读并应用;**锁屏唤醒亮度也读它**
    (`lock.c` 的 `SCR_FULL` 改用 `settings_brightness()`,不再写死 `0xFF`)。
  - **音量**:存值(0-100)+ NVS,当前已接 `audio_out.c` 的 ES8311/I2S 输出。设置页可播放短提示音,
    倒计时结束可播放闹铃;麦克风与扬声器共用 I2S0,各 App 退出时必须完整释放后再切换。
- `app_settings.c`:两个 `lv_slider`(主题红强调),`VALUE_CHANGED` 实时应用、`RELEASED` 存 NVS。
- 启动器图标 `ic_settings`(三条滑轨,中间旋钮红)。`APPS[]`/`ICON_FN[]` 都加到第 6 项。
- **⚠ QSPI 命令编码坑(亮度一直不生效的根因)**:CO5300 走 QSPI 时,任何直接发的命令都要按 QSPI 编码 ——
  `cmd = (0x02 << 24) | (real_cmd << 8)`(`0x02` = 写命令 opcode)。驱动内部 `tx_param` 自动这么做,
  所以 init 的 `0x51` 没问题;但 `display_set_brightness` 之前发的是**裸 `0x51`**,面板收不到。
  已修(`display.c`)。以后任何 `esp_lcd_panel_io_tx_param` 直接发 CO5300 命令都得这样编码。

## 音频可视化 app(第 7 个,**已随整机烧录启动 —— 音频采集仍需实机进入 app 看 `rms=` 验证**)

- `audio_mic.c`/`.h`:麦克风采集。**ES7210(4 路 TDM)+ I2S RX(master)→ esp_codec_dev**,读单声道 16bit @ 16kHz。
  配置对齐小智 `BoxAudioCodec` 输入侧;I2S 用 `I2S_CHANNEL_DEFAULT_CONFIG` / `I2S_TDM_*_DEFAULT_CONFIG` 宏(跨 IDF 版本稳)。
  ES7210 总线 7-bit 地址是 `0x40`,但 `esp_codec_dev` 的 `audio_codec_i2c_cfg_t.addr` 要传 8-bit
  写地址 `ES7210_CODEC_DEFAULT_ADDR`(`0x80`,组件内部再右移为 `0x40`);当前输入增益为 40dB。
  新增依赖 `espressif/esp_codec_dev`(组件管理器首次 build 下载)+ `esp_driver_i2s`。
- `app_audio.c`:独立任务读一帧(480 样本)→ 对 18 个对数分布频点跑 **Goertzel** → `s_band[]`(快上慢下平滑);
  `audio_tick` 把 18 根竖条按能量设高度 + 颜色(低青/中黄/高红,**音频 viz 特意破单色**)。任务里 `ESP_LOGI("audio","rms=%.0f")` 供验证拾音。
- **风险**:① 首次 build 要下 `esp_codec_dev`,API 字段万一对不上会编译报错(照报错改);② 采集能不能真拾到音
  得看串口 `rms=` 有没有随声音跳 —— 这部分我没法离线测,大概率要在板上调增益/格式。退出时任务自己 `audio_mic_stop` 收尾。
- **⚠ 内存坑(加音频后开机黑屏)**:`esp_codec_dev`+I2S 链进来后内部 RAM 吃紧,LVGL 那块 466×160 单缓冲(~146KB 内部 DMA RAM)
  分配失败(`lvgl_port_add_disp ... Not enough memory for buf1`)→ 显示根本没起来 = 黑屏。把缓冲从 **160 行降到 80 行**
  (`display.c` `buffer_size = LCD_H_RES*80`,~73KB)就放得下了。再加 app 若又不够,继续降行数(别放 PSRAM,本板 PSRAM+DMA 点不亮)。
- 另:亮度下限抬到 `SETTINGS_BRIGHT_MIN=0x40`(25%)+ 开机夹住,防止存了过低亮度导致开机看着黑屏(和上面那个是两回事)。
- **⚠ ES7210 要 AXP2101 ALDO1 供电(写 0x40 全 NACK 的根因)**:音频 codec(ES7210/ES8311)挂在 **ALDO1**(3.3V)。
  小智在 `Pmic` 构造里 `0x92=(3300-500)/100` 设压 + `0x90 |= bit0` 开 ALDO1。我之前 `power.c` 只读 AXP2101、没开这轨 →
  ES7210 没电 → I2C 写 `0x40` 全 NACK → `es7210 open fail`。已加 `power_audio_on()`(只开 ALDO1、不碰其它轨),
  在 `audio_mic_start` 开头调用 + 60ms 上电延时。I2C 扫描看到 `0x40` 是对的,但传给
  `esp_codec_dev` 控制器的配置值必须是 8-bit `0x80`。

## 数字孪生退出 + 充电 95% 修复

- **twin app 退出卡死**:`go_home()` 在 LVGL 任务里同步调用 `cur_app->exit()`。旧版 `ble_twin_stop()` 在退出里直接
  `nimble_port_stop()` + `nimble_port_deinit()`,而 IDF NimBLE 的 `nimble_port_stop()` 会用永久等待等 host stop 完成;
  一旦 stop/deinit 与 GAP 事件、连接断开或 WiFi 恢复抢时序,LVGL 任务就可能卡住。第一次只加 `s_stopping` 阻止
  `DISCONNECT`/`ADV_COMPLETE` 重新广播,但仍保留同步 stop/deinit,实机退出仍会卡。
  当前修正:BLE host 跟 WiFi service 一样只初始化一次;twin 退出只清 RX 回调、停广播、断开当前连接、关闭前台 active 标志,
  不再在 LVGL 任务里 deinit NimBLE。后续注意:任何 app 的 `exit()` 都会跑在 LVGL 任务里,不要在里面做不可控的长阻塞;
  对系统协议栈优先采用"服务常驻 + 前台启停"模型,不要每次 app 退出都强制 deinit。
  ```mermaid
  flowchart TD
      A["进入 twin app"] --> B["ble_twin_start"]
      B --> C{"NimBLE host 已运行?"}
      C -- "否" --> D["初始化 NimBLE host + 注册 GATT"]
      C -- "是" --> E["仅打开 active 标志"]
      D --> F["host sync 后开始广播 GeekTwin"]
      E --> F
      F --> G["采样任务 notify IMU/电量帧"]
      H["退出 twin app"] --> I["停止采样 + 清 RX 回调"]
      I --> J["关闭 active 标志"]
      J --> K["停广播/断开当前连接"]
      K --> L["保留 NimBLE host,返回启动器"]
  ```
- **充电最多约 95%**:`power.c` 原来沿用 xiaozhi 示例的 AXP2101 CV 4.1V 保守档(`0x64=0x02`)。
  对普通 4.2V 锂电,4.1V 截止常会让电量计停在 95% 左右。已改为 `0x64=0x03`(4.2V),保留预充 50mA、终止 25mA、
  默认输入限流和温度降流策略不变。后续注意:不要改到 4.35V/4.4V,除非确认电池本身是高压锂电。

## 2026-07-19 代码与文档一致性优化

### 状态

- 已实现、已完成本机构建与浏览器验证

### 背景与当前行为

- 仓库代码与 Tag 已到 `v1.7-beta.5`,启动器实际注册 16 个 App,但根 README 仍停在
  `v1.4 / 15 个 App`,且本文件早期的 OTA URL、音频输出和 ES7210 地址说明已经被后续实现推翻。
- OTA 任务允许离开页面后在后台继续下载,但再次进入页面会把 `s_state/s_pct` 重置为空闲,
  UI 与仍在运行的任务脱节。
- `audio_mic_start()` 在 I2S 或 codec 初始化中途失败时直接返回,不会释放前面已经创建的对象;
  再次进入音频 App 可能继续泄漏资源或因 I2S0 被占用而失败。
- Web 蓝牙连接在 GATT 服务发现或通知订阅中途失败时没有统一清理设备事件和连接;Three.js 场景也随首包同步加载。

### 目标、范围与非目标

- 目标:修复上述生命周期问题,降低 Web 首包耦合,并让入口文档反映当前真实代码。
- 范围:`app_ota.c`、`audio_mic.c`、`app_audio.c`、Web 连接/场景入口、根 README、Web README 与本记录。
- 非目标:不升级 ESP-IDF 组件版本,不调整硬件参数、分区表、BLE 协议、OTA 发布地址或现有 UI 视觉。
- 本轮只做本机构建与静态检查;没有连接开发板,因此不把 OTA、音频、BLE 或屏幕效果标记为真机通过。

### 调用链与方案

```mermaid
flowchart LR
    L["Launcher 进入 App"] --> O["OTA 页面"]
    O --> T["后台 OTA task"]
    T --> S["持久运行状态/进度"]
    S -->|"离开后重进时恢复"| O
    L --> A["Audio App"]
    A --> M["audio_mic_start"]
    M -->|"任一步失败"| C["统一释放 codec / I2S"]
    W["Web 连接按钮"] --> G["GATT 建连与订阅"]
    G -->|"中途失败"| D["移除监听并断开"]
    W -->|"独立异步块"| R["Three.js 场景"]
```

### 验收、风险与回滚

- `idf.py -C GeekTool-IDF build` 通过,并记录镜像大小与分区余量。
- `npm --prefix web run build` 通过,对比拆分前后的产物体积。
- `git diff --check` 通过,README 的版本、App 数量、音频与 OTA 描述能由当前代码佐证。
- 风险集中在异步退出/重入时序;实现保持后台 OTA 策略不变,只恢复真实状态。若真机发现回归,
  可按文件回退本节涉及的局部生命周期修改,不需要更改 NVS、分区或发布对象。

### 实际实现结果

- `app_ota.c` 不再在页面进入时清空后台状态;重进会重置 UI 去重缓存,再由首个 `ota_tick`
  恢复检查中、下载进度、成功、失败或已是最新状态。重新发起更新时仍会把进度归零。
- `audio_mic.c` 对 TDM、data/control interface、ES7210 codec、codec device 和 open 的失败分支
  统一调用幂等清理;`app_audio.c` 在建任务前置位 alive,并在旧任务仍未退出时拒绝重复创建。
- Web GATT 服务发现或通知订阅中途失败时会移除事件监听并断开;Three.js 场景使用
  `lazy + Suspense`,姿态算法只直引 Three 数学子模块,避免渲染器回流主包。
- 固件构建通过:`GeekTool.bin` 为 `0x1ced30`,3 MiB OTA 分区剩余 `0x1312d0`(40%)。
- Web 类型检查与生产构建通过:主包从 `620.71 kB` 降到 `172.11 kB`(约 -72%),
  Three.js 场景独立为 `462.85 kB`,不再出现 500 kB chunk 告警。
- 应用内浏览器在 `1280 x 720` 和 `390 x 844` 验证页面非空、canvas=1、无错误覆盖层、
  干净会话无 console error/warn;点击“重置”后可见状态变为“校准已重置”,移动视口无横向溢出。
- `git diff --check` 通过。未连接开发板,OTA 后台重入、ES7210 失败重试和真实 BLE 连接仍需真机复验。

### 与原计划的偏差

- 初次只动态导入 `CubeScene` 时,Three 顶层入口仍被姿态算法共享,主包几乎没有下降;随后改为
  直接引用 `Quaternion/Vector3/MathUtils` 子模块,才实现真实拆包。没有扩大到依赖升级或 UI 重设计。

## 2026-07-19 第二轮异常路径优化

### 状态

- 已实现、已完成本机构建与静态验证

### 当前行为与范围

- 天气任务未检查 `esp_http_client_init()` 返回的句柄;内存紧张时可能带着空句柄继续 open/close。
- 天气和鼠标 App 用跨页面保留的静态值去重;重进页面时如果连接/失败状态未改变,
  新创建的标签可停在“正在启动”或“正在获取”。
- IMU 探测只执行一次,配置寄存器写入失败仍会标记 ready;瞬时 I2C 异常后也不会恢复。
- 本轮限定修改 `app_weather.c`、`app_mouse.c` 和 `imu.c`,不改天气 API、BLE HID 协议、
  IMU 量程/坐标映射、NVS 数据或 UI 布局。

### 方案与验收

```mermaid
flowchart LR
    E["App 进入"] --> R["重置 UI 去重缓存"]
    R --> S["首次 tick 强制回放当前状态"]
    H["天气 HTTP 初始化"] -->|"句柄或缓冲区失败"| F["可控失败并释放已有资源"]
    I["IMU 探测/配置"] -->|"失败"| C["5 秒冷却后允许重试"]
    I -->|"所有写入成功"| O["标记 ready"]
```

- 天气 HTTP 对 client/body 失败分支不解引空句柄,任务仍能正常退出并允许后续重试。
- 天气/鼠标每次进入都回放当前状态,并恢复与状态匹配的文字颜色。
- IMU 只在全部配置写入成功后标记可用;失败后限频重试,避免每帧冲击 I2C。
- 固件构建、`git diff --check` 与相关静态检查通过;未接开发板时,保留天气断网恢复、
  BLE 重连和 IMU I2C 故障注入为真机待验证项。

### 实际实现结果

- `app_weather.c` 对 HTTP client 和 8 KiB body 分配分别判空;只有 open 成功才 close,
  只要 client 存在就 cleanup,异常分支最终设为 `WX_FAIL` 并释放 body。
- 天气页进入时重置 `s_shown`,加载/成功时把状态文字颜色恢复为次要色;
  鼠标页用 `s_link_state_known` 保证首次 tick 必定显示配对或已连接,且启动失败文案不会被覆盖。
- `imu.c` 检查 CTRL1/2/3/7 全部写入;探测或配置失败会移除已添加的 I2C device,
  后续读取按 5 秒冷却重试,不再将部分初始化当作 ready。
- ESP-IDF 6.0.1 完整构建通过:`GeekTool.bin` 为 `0x1cef40`,3 MiB OTA 分区剩余
  `0x1310c0`(40%);Web 类型检查与 Vite 生产构建再次通过,主包 `172.11 kB`,异步场景 `462.85 kB`。
- `git diff --check` 通过。本轮未接开发板,因此天气断网/内存紧张分支、BLE 鼠标重进与
  QMI8658 故障注入仍是真机待验证,不作真机通过表述。

### 与计划的偏差

- 无。未扩大到 NVS 自动擦除或更底层的 I2C 总线重置,避免引入用户数据丢失或跨模块影响。

## 2026-07-19 OTA 圆屏视觉重构（Orbit Console）

### 状态

- 已实现、已完成本机构建与 466×466 离线视觉 QA;真机待验证

### 背景与当前行为

- 真机照片显示 OTA 页同时存在全局 458px 充电/电量环和页面内 268px 完整进度环,形成重复的
  “靶心”视觉;版本文字压在内环上沿,标题、返回键、版本和主操作缺少统一纵向秩序。
- 空闲态中央只有下载符号,点击含义依赖底部提示文字;“测试通道”标签和开关又独立贴在屏幕下沿,
  主操作、状态说明和次要设置没有形成清晰层级。
- OTA 后台任务、退出后继续下载、再次进入恢复状态等行为已经稳定,本轮只重构 OTA 页的
  LVGL 表现层,不改下载地址、版本比较、任务生命周期、分区或发布协议。

### 目标、范围与非目标

- 采用已确认的 **Orbit Console** 方向:保留全局外环作为设备状态边界,页面内部改为顶部留口的
  点阵轨道,把“检查更新”作为唯一主操作,底部测试通道收进一枚紧凑胶囊。
- 覆盖 `空闲 / 检查中 / 下载中 / 成功 / 已是最新 / 失败` 六种页面状态;所有状态沿用项目已有
  黑底、白灰点阵、红色交互强调和绿色成功语义,不引入图片资源或新的渲染依赖。
- 非目标:不改启动器全局标题/返回/电量层的通用布局,不新增 OTA 能力,不修改 Wi-Fi 与 OTA 业务逻辑,
  不扩大到其他 App 的视觉重构。

### 信息层级与状态流

```mermaid
stateDiagram-v2
    [*] --> Idle: 进入页面
    Idle --> Checking: 点击“检查更新”
    Checking --> Running: 发现新版本
    Checking --> UpToDate: 当前已是最新
    Checking --> Failed: 网络或服务异常
    Running --> Success: 下载与校验完成
    Running --> Failed: 下载或校验失败
    Failed --> Checking: 点击重试
    UpToDate --> Checking: 再次检查
```

- 顶部:全局返回键与“在线更新”标题,下方只显示一行当前版本。
- 中部:开放式点阵轨道承载检查动画或下载进度;中央图标、百分比和主操作文案保持同一视觉焦点。
- 底部:状态说明位于轨道下方;测试通道作为次要设置固定在独立胶囊内,不与主操作争夺注意力。

### 实施步骤与验收标准

1. 移除 OTA 页完整内环和无语义的上下漂浮动画,建立开放式点阵轨道与统一状态渲染函数。
2. 重排版本、中央图标/百分比、状态说明、主操作热区与测试通道胶囊,保持 466×466 圆屏安全区。
3. 保留现有触摸行为和后台状态恢复;运行态以轨道显示真实进度,其余状态提供明确反馈与重试入口。
4. ESP-IDF 完整构建和 `git diff --check` 必须通过;静态检查确认六种状态均有明确视觉输出。
5. 视觉验收:不再出现双完整圆环,文字不压线、不贴屏幕边缘,空闲态无需阅读说明即可识别主操作,
   测试通道保持可触达但视觉降级。离线预览只用于几何核对,最终 AMOLED 亮度、触摸命中和动态流畅度
   仍以真机照片/操作为准。

### 风险与回滚

- 点阵对象比单个 `lv_arc` 更多,但数量会控制在与现有秒表刻度同一量级;若真机刷新或内存出现回归,
  可只回退 `app_ota.c` 的表现层,不会影响 OTA 任务和分区数据。
- 全局充电环颜色由电池状态决定,页面不能假设外环始终为绿色;内部成功态仍需靠图标和文字独立表达。

### 实际实现结果

- `app_ota.c` 删除页面内完整 `lv_arc` 和上下弹跳动画,改为 54 个 LVGL 原生点组成的 260° 开放轨道。
  空闲态为白色轨道+红色起点,检查态由红色前沿沿轨道运行,下载态显示真实百分比、白色已完成段和红色前沿。
- 中央下载/对勾/叉继续复用项目 `glyph_line` 点阵语言;主操作文案改为“检查更新”,空闲、失败和已最新态
  共用 214×178 热区,检查/下载/成功待重启时自动禁止重复点击。
- 测试通道改为 192×48 底部胶囊,保留原设置持久化;检查、下载和成功待重启阶段禁用开关,
  避免用户将中途切换误解为会改变当前任务的下载通道。
- 六种状态在 466×466 本地几何预览中逐项核对,并将选定参考与空闲态预览按同尺寸并排比较;
  轨道顶部留口、版本行、主操作、状态反馈和底部胶囊均无可见越界或文字压线。
- ESP-IDF 6.0.1 完整构建通过:`GeekTool.bin` 为 `0x1cf500`,3 MiB OTA 分区剩余
  `0x130b00`(40%);`git diff --check` 通过。设计核验记录见根目录 `design-qa.md`。
- 本轮没有烧录开发板,因此 AMOLED 实际亮度、54 点轨道动画流畅度、主操作/开关触摸命中、六状态真机画面
  仍列为待验证,不以浏览器几何预览替代真机结论。

### 与计划的偏差

- 选定概念的底部胶囊更窄;实现为容纳现有 54×28 开关、中文标签和触摸间距扩大到 192px。
  其余偏差仅来自全局电量环和字体属于启动器共享层,本轮按非目标保持不变。

## 2026-07-20 设置页三分类视觉重构

### 状态

- 已实现、已完成本机构建与 466×466 离线视觉 QA;真机 OTA 后待验证

### 背景与当前行为

- 真机设置首页把显示、声音、系统的 7 个具体选项连续放进多张 306px 深色矩形卡片。虽然功能完整,
  但圆屏首屏主要被一个手机式列表卡占据,所有行视觉权重接近,分组标题与内容脱节,下方内容也缺少明确的层级预告。
- 当前返回关系只有“全部选项菜单 → 单项详情”两层;本轮按确认方案改为“设置首页 → 分类 → 单项详情”三层,
  让首屏只承担分类选择,具体设置在分类内出现。
- 亮度、表盘、常显、音量、静音、语言、关于的现有业务回调、NVS 存储、音量试听与运行时立即生效行为
  均为兼容约束,不能因视觉重构改变。

### 目标、范围与非目标

- 设置首页只显示 `显示 / 声音 / 系统` 三个大触摸入口,带实时摘要、点阵图标和次要箭头;
  中间“声音”入口用轻表面和红色焦点点强调,上下入口退为黑底描边,复现选定方案的视觉层级。
- 分类页使用轻量独立行展示该分类内的具体选项,不再用一个大矩形把所有内容包住;详情页继续使用适合圆屏的
  大读数滑块、大开关、表盘/语言选择和 About 信息。
- 非目标:不修改设置数据结构、默认值、亮度下限、音量输出、AOD/静音语义、表盘资源或全局电量环/返回键布局。
- 本次与上一节 OTA Orbit Console 改动共同进入下一测试版;发布前必须同时通过固件构建和代码一致性检查。

### 导航与动画

```mermaid
stateDiagram-v2
    [*] --> Hub: 进入设置
    Hub --> Display: 点显示
    Hub --> Sound: 点声音
    Hub --> System: 点系统
    Display --> Detail: 亮度 / 表盘 / 常显
    Sound --> Detail: 音量 / 静音
    System --> Detail: 语言 / 关于
    Detail --> Display: 返回显示分类
    Detail --> Sound: 返回声音分类
    Detail --> System: 返回系统分类
    Display --> Hub: 返回
    Sound --> Hub: 返回
    System --> Hub: 返回
    Hub --> [*]: 返回启动器
```

- 动画只作用于 3 个首页入口或当前面板:入场错峰 `40ms`,位移不超过 `14px`,时长 `160–220ms`,
  使用 ease-out/ease-in-out;不做全屏大距离滑动、不做常驻循环动画,避免 QSPI 刷新面积和对象更新量过大。
- 页面切换先建立目标内容,再做短淡入和小位移;离场不等待动画、不在回调内阻塞,确保返回栈和音频释放稳定。
- 按压反馈只改变当前行的背景/边框,点击区不小于 `64px` 高。

### 实施与验收

1. 把原一级连续菜单拆为三分类首页与分类列表,补齐 `首页 / 分类 / 详情` 状态和逐级返回。
2. 用项目原生 `glyph_*` 点阵能力绘制显示、声音和系统图标,使用现有黑/白/灰/红 token,不新增图片资源。
3. 首页摘要必须实时反映亮度、当前表盘、音量、静音和语言;从详情返回时重建对应层级刷新数值。
4. 验证三分类、7 个详情入口、亮度/音量实时反馈、开关、表盘、语言、About 与全部返回分支。
5. ESP-IDF 6.0.1 完整构建、`git diff --check`、视觉对照和动画/生命周期代码审查通过后,
   提交并打下一个 beta tag;再核对 Actions、GitHub prerelease 与 R2 测试通道资产。
6. 浏览器几何预览只能验证 466×466 布局与交互层级;实际帧率、触摸命中、拖动手感和屏幕亮度仍需真机 OTA 后确认。

### 实际实现结果

- `app_settings.c` 已从原来的“两层连续长列表”改为 `首页 → 分类 → 详情` 三级状态机。首页只显示显示、声音、
  系统三枚 72–82px 高胶囊;分类页分别承载 3、2、2 个独立设置行,7 个原有详情和逐级返回路径全部保留。
- 首页摘要会在返回时重新读取亮度、表盘、音量、静音、语言和 About 文案;语言切换后当前详情标题也立即刷新,
  不再出现中文内容配英文/旧标题的短暂不一致。
- 首页图标继续使用 `glyph_circle`、`glyph_line`、`glyph_arc` 原生点阵对象。首页和分类行只做一次
  `12px / 180–190ms` ease-out 入场,最多 3 行、间隔 40ms;详情面板只做一次 180ms 淡入/短位移,
  `app_settings.tick` 仍为 `NULL`,不存在常驻动画或整屏连续重绘。
- 音频输出从“进入设置立即初始化”改为“首次打开音量详情时按需初始化”,退出设置统一释放;浏览亮度、表盘、
  常显、语言或 About 时不再承担 codec/I2S 初始化延迟和资源占用。
- 滑块、开关、选中列表统一使用黑/灰/白/红 token,补齐按压边框反馈;详情标题交给启动器共享标题层,
  删除了详情页内重复的小标题,为控件和说明留出更稳定的圆屏安全区。
- ESP-IDF 6.0.1 完整构建通过:`GeekTool.bin` 为 `0x1cfef0`,3 MiB OTA 分区剩余
  `0x130110`(40%);`git diff --check` 与预览工程生产构建通过。466×466 参考/实现并排视觉核对、
  `首页 → 显示 → 亮度 → 显示 → 首页` 交互回归和浏览器错误日志检查均通过,记录见根目录 `design-qa.md`。
- 本轮未在真机安装测试版,因此 AMOLED 像素观感、实际触摸命中、滑块拖动手感与动画帧率仍保留为 OTA 后验收项。

### 与计划的偏差

- 选定概念中的外环是极细白线;实际产品继续使用启动器共享的 8px 电量环,本设置 App 不越权修改全局层。
- 浏览器预览用图标库模拟点阵轮廓来核对几何,固件实际使用项目原生 `glyph_*` 点对象;布局、尺寸、颜色层级
  与选定方案一致,但最终图标像素密度以真机固件为准。
