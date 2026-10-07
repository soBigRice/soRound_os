# 联网请求与恢复逻辑

核对日期：2026-10-07；ESP-IDF 6.0.1 / ESP32-S3，基于 `main / b626cbd` 的未提交工作区。
本轮本地修复和自动验证已完成；用户暂不方便连接设备，OTA 后真实联网故障的完整根因仍待串口证据确认。
现有证书包、Mbed TLS 的 PSRAM 分配、Wi-Fi FLASH 凭据、NVS/分区与OTA下载校验保持；
本轮随后接入 [首启自检](./STARTUP.md)，核心检查后才确认新固件，联网失败允许离线启动。

## 范围与入口

核对 `main` 中的 URL、HTTP/TLS、socket 和 SNTP 调用，当前外部请求入口如下。

| 功能 | 实际调用链与触发 | 已确认的边界与本轮修正 |
| --- | --- | --- |
| 天气及天气表盘 | `weather_poll → start_fetch → wx_task → weather_data_url → network_http_get → weather_data_parse → generation → s_data/s_revision` | Open-Meteo HTTP；成功20分钟、失败1分钟刷新；8KiB响应、1KiB请求发送缓冲保持。IP恢复后 `WX_OFFLINE` 立即重试；完整SDK响应才解析；切换城市作废旧generation。退出天气页保留共享后台请求/缓存，表盘共用这一来源。 |
| 答案之书 | `start_turn → answers_fetch_begin → fetch_task → network_http_get → answers_data_parse → answers_fetch_poll → poll_answer` | 小小API HTTPS；每次翻页生成新随机附加值，同次重试保留该值；4KiB缓冲。快速退出重进遇到旧worker `BUSY` 时先等待其清理，再发当前请求；48条备用、缺字/过长/重复处理、显示后不被迟到响应替换保持。 |
| 星座运势 | `refresh → zodiac_fetch_begin → fetch → network_http_get → zodiac_data_parse → zodiac_fetch_poll → zodiac_tick` | 小小API HTTPS；8KiB缓冲；固定所选星座和请求日，核对UTF-8、业务码、评分及当天日期。保留BUSY等待、刷新/切换/退出取消；校时跨日后，即使此前失败/离线也重新请求一次。 |
| OTA | `start_btn → ota_task → ota_update_run → esp_https_ota_* → ota_tick` | 正式/beta的固定HTTPS入口。Wi-Fi已开启但IP尚未就绪时，worker最多等待15秒；超时先停止，尚未打开/写入固件。保留最多三次尝试、强ETag/If-Match/Range续传、项目/版本/长度/固件完整校验与失败清理。错误保留DNS返回值、TCP errno及TLS code/flags，UI呈现相应阶段。 |
| Wi-Fi / 校时 | `wifi_service_start → wifi_svc_init → wifi_evt → esp_sntp_init/restart → on_time_sync → rtc` | `pool.ntp.org` UDP校时在第一次GOT_IP后启动，后续GOT_IP重新校时。扫描抑制期间掉线，扫描完成/失败或离页取消扫描后恢复已保存网络的连接；原认证/超时状态和后台服务保持。 |
| 启动联网检查 | `app_main → startup_network_check → boot_net → esp_http_client` | 核心检查/OTA确认后，无IP立即离线；有IP时对同一OTA通道做一次HTTPS HEAD，主任务最多等8.5秒，不下载正文/写flash。HTTP、DNS、TCP、TLS失败记录并提示，不影响固件有效性或离线进入表盘。 |
| 日期、时钟、网络状态表盘 | RTC/系统时间；`watchface.c:snapshot → wifi_service_ready / weather_cached` | 没有另一套HTTP请求。Wi-Fi状态和IP采用同一就绪判断，仍按原有分钟/强制刷新时机更新。日历、时钟间接使用SNTP结果。 |

BLE鼠标/设备连接属于本地蓝牙通信，其余离线工具没有额外Internet请求；未为离线功能增加联网要求。
每项的详细UI说明仍在 [天气](./WEATHER_DETAILS_DESIGN.md)、[答案之书](./ANSWERS_UI.md)、[星座](./ZODIAC_MERIT_UI.md)、[Wi-Fi](./SETTINGS_LOCATION_DESIGN.md) 和 [表盘](./WATCHFACES_UI.md)。

## 无线服务就绪与恢复

`wifi_service.h` 为现有 `app_wifi.c` 服务的独立声明入口。
`wifi_service_ready()` 要求：开关开启、已收到GOT_IP、默认STA netif存在且up、AP仍关联、IP读取成功且IPv4非零。
`STA_DISCONNECTED / STA_STOP / STA_LOST_IP` 清除GOT_IP状态。AP关联成功不等于DHCP完成；
IP就绪也不证明具体DNS、互联网或服务可达，后者由各请求的实际结果确定。

扫描时原来会抑制断线事件里的自动重连，以允许扫描完成；`resume_saved_connection` 在扫描结束/失败/取消离页后补上这一恢复点。
仍在手动连接、密码操作抑制或Wi-Fi关闭时不抢占该流程；已关联时不主动断开。
SNTP配置在 `esp_wifi_start` 前设置，避免快速GOT_IP先于校时配置；事件回调不操作LVGL。

## 三个数据App的共享HTTP边界

`network_http.c:network_http_get` 只负责有界传输；调用方继续持有worker、generation、缓冲与业务解析器。
采用项目已有ESP-IDF API，避免三份读循环分别遗漏就绪判断、取消、完整性和错误清理；没有新增依赖或后台网络管理器。
OTA使用专用SDK下载/续传链路，不套用JSON读循环。

```text
generation有效 + IP就绪
  → init / 请求头 → open → fetch_headers → HTTP 200 / 容量检查
  → 分段read（每次最多1024B，调用间检查取消和预算）
  → 非空 / SDK完整正文 / 容量 / 预算检查
  → close + cleanup → 业务解析 → generation相同时发布
失败 → 在close前取得 transport_error / tls_code / tls_flags / socket_errno
     → cleanup → 记录阶段、HTTP、字节数、内部堆/最大块/PSRAM
     → 符合瞬态条件且剩余预算足够时，等待200ms重试一次
```

| 情况 | 行为 |
| --- | --- |
| 连接、响应头、读取、中途不完整；HTTP408或5xx | 最多两次尝试，共用一次请求预算；取消、离线或预算耗尽时停止。 |
| 证书flags、X509验证/fatal、SSL/X509分配失败（正负错误码均检查） | 停止重试，保留证书校验和原内存策略。 |
| 其他4xx、初始化/设置请求头失败、响应过大 | 停止重试；失败原因保留在日志。 |
| JSON、UTF-8、业务字段、星座/日期等解析失败 | 由各解析器拒绝，不将HTTP200视为业务成功，不循环重试坏内容。 |
| 退出/换城市/换星座/等待超时 | 作废generation；任务在SDK调用边界检测后自行关闭连接和释放内存，不在SDK I/O中强杀任务。 |

答案/星座I/O上限6秒、共享传输预算8秒；天气I/O上限12秒、预算24秒。
预算在SDK调用前后检查，并将下一次I/O timeout缩至剩余预算；SDK内部阻塞DNS或读取可能超过检查点，
因此这些数字不是整个worker的硬墙钟终止保证。数据App的LVGL线程不执行这些阻塞操作。
答案/星座UI等待新请求最多8.5秒；若先遇到旧worker BUSY，另有最多8.5秒的清理等待，开始新请求后重新计时。
两阶段不能合计为8.5秒，也不能用强杀任务来换取表面超时。原有返回/锁屏仍可用。

数据App日志使用 `network_http_stage_t` 定义的阶段；解析失败另记HTTP/字节数。
OTA的 `esp_http_client_get_and_clear_last_tls_error` 返回值是ESP-TLS传输错误，DNS失败可能只有这个返回值，
不能只读取它的两个Mbed TLS输出参数。`http_event` 在 ERROR/DISCONNECTED 间保留非零错误，防清理时丢失。
新增诊断日志不包含Wi-Fi密码、用户地点坐标、完整请求/正文或设备身份。

## 证据、局限与防复发

- 10组目标测试通过：天气、答案UI/网络、星座UI/网络、Wi-Fi、表盘、OTA UI/恢复，以及已有设置修订回归；使用实际App/worker/parser和LVGL9.5，网络/设备边界为夹具。
- 天气IP恢复回归用原HEAD源码在独立临时可执行文件复现：恢复后60秒内未发请求；当前源码立即发起且通过。还覆盖DHCP未完成、LOST_IP/STOP、SNTP时机、扫描中断重连、BUSY交接、过期代次、完整JSON但传输不完整、瞬态重试、坏响应/证书拒绝及资源清理。
- ESP-IDF6.0.1 / ESP32-S3联网修订构建和现有 `verify_image` 段边界/XOR/附加SHA核验通过。历史候选版本、SHA、容量与测试回执在 `build/flash-records/network-recovery-20261007/`；随后启动修订已覆盖当前 `build/GeekTool.bin`，其精确摘要见 `build/flash-records/startup-selftest-20261007/`。两者均包含此前待验收的设置修改，尚未提交、发布或烧录。
- 2026-10-07桌面curl对照天气/答案/白羊接口均200，实际三份解析器通过；OTA beta只核对1024B Range206，并非整包下载验收。另用设备默认User-Agent与TLS1.2请求星座返回200。此前Python TLS EOF与curl结果不同，不能据此断言提供方停机/兼容性故障，更不能据桌面成功判定手表恢复。
- OTA DNS/TCP状态的 [两张466×466原生图](./artwork/network-recovery/README.md) 已查看，使用错误与电量夹具。布局检查不代替设备升级。

用户反复反馈“Wi-Fi已连接但App失败”，此前检查缺少AP关联/DHCP/请求阶段的区分和恢复时序。
以后对此症状先查 `wifi_service_ready` 与同一次请求的阶段/HTTP/transport/errno/TLS/内存证据，
再区分提供方/解析器，不默认清缓存、改DNS、增加缓冲或关闭证书验证。
历史beta.23 TLS内部堆不足有实机证据；本轮仍采用已批准的PSRAM分配，不能把它当作所有后续联网失败的已证实原因。
当前证书日期检查未启用，SNTP修正用于日期与联网时序，不能说它证明TLS日期检查导致此次故障。
`main.c` 当前OTA确认已经移至公共资源、UI心跳/DMA、图片就绪及至少4秒检查之后。
随后进行的启动HEAD仅证明选定更新入口的TLS/HTTP可达，不证明各App服务或真实OTA升级成功；具体边界见 [启动逻辑](./STARTUP.md)。

用户方便连接后，最低设备验收：

1. 重启后DHCP完成前后分别打开四项；天气断网恢复、扫描中掉线后退出再连；确认已保存网络保留。
2. 答案快速退出再进、星座快速换项/跨日校时；打开天气同时使用HTTPS App，检查迟到内容与内存峰值。
3. A→B完整OTA、重启后四项仍联网，再从B更新到下一不同版本；覆盖断网续传、坏包拒绝与原启动失败回滚。
4. 失败时保留当前版本、同一次请求日志及相邻Wi-Fi事件；不将重启后一次正常、桌面200、构建通过或发布完成当作根因结案。

## 核对来源

官方版本与本机同为6.0.1；2026-10-07核对SDK头文件及直接实现：

- [ESP32-S3 Wi-Fi事件及IP时机](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/api-guides/wifi-driver/overview.html)：GOT_IP之后再开始socket工作，断开后旧TCP连接失效。
- [esp_http_client.h](https://github.com/espressif/esp-idf/blob/v6.0.1/components/esp_http_client/include/esp_http_client.h) / [实现](https://github.com/espressif/esp-idf/blob/v6.0.1/components/esp_http_client/esp_http_client.c)：timeout、完整响应、TLS getter返回值及cleanup契约。
- [SNTP实现](https://github.com/espressif/esp-idf/blob/v6.0.1/components/lwip/apps/sntp/sntp.c)：首次init与后续线程安全restart；不重建整个无线服务。
