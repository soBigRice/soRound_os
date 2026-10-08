# 启动放行与 OTA 首启自检

核对日期：2026-10-08；beta.30 (`5efc052`) 因用户实际 OTA 重启白屏反馈撤回，当时分发恢复 beta.29。
修订版 beta.31（源提交 `2aef1e0`）已发布；设备已确认旧版发生首启未确认回滚，原白屏的具体触发点仍未复现。
用户已确认：至少4秒动画、初始化完成才放行；联网失败记录并提示，允许离线启动；完成后进入原有锁屏表盘。
修订版通过本地ESP-IDF6.0.1构建、LVGL9.5全套34组测试、本机GCC15的9组启动检查及发布CI同版LVGL9.6的9组启动回归。
正式依赖候选的USB首启/表盘/解锁/设置已由用户确认；最终beta.31发布包的真实OTA检查待继续（USB已断开）。
测试执行真实 `app_main → launcher_start → lock/watchface → startup`，硬件、RTOS与外部服务为夹具，不能据此认定玻璃面板/真实OTA已通过。
回执在 `build/flash-records/ota-white-screen-20261008/`；原生预览见 [启动审阅](./artwork/identity/native/startup-20261007/README.md)。

## 入口与职责

```text
main.c:app_main
  → NVS / audio_bus / I2C / IMU / RTC / display / settings / i18n / touch
  → LVGL锁内：identity_boot_create → launcher_start → 覆盖层移至最前
  → 锁外：图片后台预热 → wifi_service_start
  → startup_selftest：核心资源 + 图片任务完成 + 活跃心跳/首帧DMA + 至少4秒
  → startup_network_check：IP就绪时，对选定OTA入口做一次HTTPS HEAD
  → 联网失败：日志 + 中英文离线提示1.2秒
  → LVGL锁内：刷新表盘 → identity_boot_reveal（保留输入/省电保护）→ display_request_frame
  → startup_wait_home：新表盘最后一块DMA完成 + 最近250ms内更新的UI心跳；最多5秒
  → startup_apply_ota_result：仅处理运行槽的PENDING_VERIFY，表盘检查失败也请求回滚
  → identity_boot_release：确认成功后删除保留的遮罩 → 原有锁屏表盘
```

`identity_ui.c` 管理覆盖层对象和单次动画，`startup.c` 管理核心检查及OTA结果；
`startup_network.c` 管理可选的联网探测，不在LVGL或主任务栈执行TLS。
`launcher.c` 暴露原20ms调度心跳，`display.c` 统计完成的面板DMA事务。
原有App按进入时初始化，不能开机同时打开全部App：音频、传感器、BLE和内存仍由各App按既有生命周期持有。
启动检查覆盖公共资源与每项注册入口，不把注册检查称为所有App功能测试。

## 核心检查与等待边界

| 检查 | 放行条件 / 失败代码 |
| --- | --- |
| 触摸 | 驱动沿用原初始化，LVGL输入设备注册成功；`TOUCH`。早期SDK致命错误仍走既有panic/reboot。 |
| 启动器 | App调度、渲染看门狗、电量、按键及省电计时器创建成功；`UI`。 |
| App表 | `APP_COUNT > 0`，每项对象/name/enter存在；`APPS`。遵守原接口，exit/tick/back等可选入口允许为空，现有日历即无exit。已核对实际19项注册。 |
| 设置存储 | NVS settings命名空间只读打开/关闭成功；首次未创建命名空间允许默认值；其他错误为`STORAGE`。不写测试数据。 |
| 公共服务 | 音频总线互斥量、Wi-Fi服务初始化完成；`AUDIO / WIFI`。Wi-Fi关联、IP、互联网不属于核心有效性条件。 |
| 内存 | PSRAM已初始化，内部堆完整性检查通过；内部RAM与PSRAM各分配512B，写读模式后释放；`MEMORY`。不扫描活跃内存或整个PSRAM堆。 |
| UI运行 | 原20ms心跳已更新且最近一次变化在250ms内，并观察到覆盖层建立之后至少一次DMA完成；`UI`。这是启动活性检查，持续运行仍由原看门狗保护。 |
| 图片 | 自定义背景预热完成；已选图片表盘还等待对应内置背景解码完成；`IMAGE`。缺失/旧出厂/不可解码的自定义图仍使用现有内置背景或提示，不改FAT分区。 |

核心资源检查后，25ms轮询一次UI/图片完成状态，最多等待5秒；同时要求从覆盖层创建起至少经过4秒。
显示回调/事件注册在LVGL锁内完成。显示初始化及main的启动阶段锁等待各有5秒上限；这不是额外动画延时。
显示或UI初建锁超时走原SDK致命错误处理，待确认固件下次启动由bootloader回滚，避免尚未创建渲染看门狗就永久卡在锁上。
UI初建后的提示/表盘锁失败不再无限等待；表盘请求为0时自检失败，错误提示为尽力显示，OTA处理继续在锁外执行。
截止时UI健康但图片未完成记`IMAGE`，UI不健康记`UI`；不会强杀仍在解码的任务。
这5秒是轮询段的边界，不是从CPU复位到表盘的总时长承诺，早期SDK外设初始化沿用原错误处理。
放行前在LVGL锁内刷新当前表盘快照，避免解码完成后仍露出旧的“Loading background”。

`identity_boot_release` 只设置ready，动画最短时长也完成后才删除覆盖层。
若初始化较慢，动画4秒结束后保留静态标志与检查文字；错误时继续遮住主页。
提前删除对象同时删除它自己的动画，不影响其他对象；释放后重置显示活动时间。
`lock.c` 在覆盖期间保持屏幕全亮、丢弃BOOT短按，保留长按关机；结束后恢复原AOD、熄屏和解锁行为。

## OTA：下载校验与运行自检

新固件必须先启动，才能执行它自己的初始化与运行自检。采用当前SDK的原生回滚机制：

```text
app_ota / ota_update：HTTPS下载 → 现有完整性/项目/版本校验 → 设新启动槽 → 正常重启
新固件第一次启动（PENDING_VERIFY）：
  核心检查通过 + 至少4秒 → 可选联网检查 → 露出表盘并确认末块DMA/新鲜心跳 → mark_app_valid_cancel_rollback
  核心检查失败 → 有可回退镜像时 mark_app_invalid_rollback_and_reboot
```

| OTA状态或结果 | 处理 |
| --- | --- |
| 普通/VALID启动 | 不确认、不标记无效；核心失败保留错误覆盖层，不主动进入重启循环。 |
| 直接烧录、运行槽没有otadata条目 | SDK返回`ESP_ERR_NOT_FOUND`时允许普通启动，不伪造OTA状态。 |
| PENDING_VERIFY核心通过 | 联网结果仅记录/提示；表盘末块DMA与新鲜心跳通过后确认有效。服务端/路由/无网故障不触发固件回滚。 |
| PENDING_VERIFY核心失败 | 先检查回滚条件，再请求SDK回滚并重启；成功重启不返回。 |
| 读取状态、确认、回滚失败或无可回退镜像 | 记录具体错误，覆盖层显示`OTA STATE / OTA CONFIRM / ROLLBACK`；不再无条件重启。 |

首次待确认启动中，若SDK初始化panic、看门狗复位或掉电，下一次启动仍由bootloader按回滚状态处理。
成功首启检查后直接进入表盘，无需再多重启一次。未更改分区、凭据/NVS格式、下载续传、TLS证书或PSRAM分配策略。
官方依据：[ESP-IDF 6.0.1 OTA / rollback](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/api-reference/system/ota.html)；
实际错误契约已核对本机 `components/app_update/include/esp_ota_ops.h`。

## 可选联网检查

`startup_network_check(settings_beta())` 使用 `ota_update.h` 的正式/beta固定URL，与OTA页面共用来源。
无IP立即记`OFFLINE`；已有IP时仅一次HTTPS HEAD，证书包校验、禁用自动重定向、I/O timeout为3.5秒，HTTP200视为此入口可达。
HEAD不读取固件正文、不写flash，不证明完整OTA下载或各数据App服务可用；各App仍按 [联网逻辑](./NETWORKING.md) 处理真实请求。

唯一8KiB worker持有SDK client；主任务最多等待8.5秒，超时只设置取消并继续离线启动。
SDK内部阻塞调用可能较晚返回，worker在下一I/O边界自行关闭/清理；不从主任务强杀它或代替它释放client。
未清理前不创建第二个worker。HTTP/TLS失败在清理前保存HTTP、transport_error、TCP errno、TLS code/flags，日志不含密码或正文。
主任务在LVGL锁内更新提示后释放锁，再等待1.2秒让提示可见；没有LVGL阻塞睡眠。

## 验证与防复发

34组LVGL9.5主机测试及发布CI同版LVGL9.6的9组启动回归通过；ESP-IDF固件构建通过。主机SDK/硬件为夹具；原设备白屏未复现。
主要回归：`tests/startup/startup_tests.c`、`startup_network_tests.c`、`lock_startup_tests.c`、`tests/host/identity_ui_tests.c`。
覆盖最短时长、两阶段图片解码、心跳/DMA失败、资源/内存/NVS失败、OTA确认/回滚错误、离线/HTTP/DNS/TLS失败、限时等待和资源清理。
启动中文/英文状态的字形与圆形边界已检查；18px状态子集独立生成，原16/26/34px字库逐段保持不变。

此前1.8秒动效自行删除、创建服务后立即确认OTA，缺少初始化完成和活性证据。
本次将“视觉时长”“核心就绪”“请求结果”分别检查；后续不得用增加延时或AP已关联替代相应证据。
最终调用链复核发现日历无exit属于合法注册，自检已遵守可选接口，并补正向回归；不能只用带全部回调的测试App推断整张注册表有效。
本轮两处主机夹具接口缺失（LVGL文本私有声明、既有按键测试的启动状态mock）已按实际接口补齐，未降低生产断言或改变UI迎合测试。

最低真机验收：无网/已保存网络各冷启动一次，确认至少4秒、联网失败提示和原表盘；图片表盘检查背景先完成；
确认长按关机及放行后的短按/AOD恢复。再做A→B真实OTA，核对`startup check`、`startup OTA action`、`boot-net`和运行槽状态。
回滚需在可恢复测试设备上使用受控失败镜像确认，不能把host故障注入当作真实设备已回滚。

### beta.30 白屏反馈后的检查边界

已用旧 `main.c` 跑同一完整启动回归，确实在表盘尚未请求/完成时确认OTA，被用例拒绝（exit42）。
这证明旧放行条件不足，**不证明它就是设备白屏的根因**。修订版通过正常/图片表盘及模拟末块DMA失败回滚。
`display_frame_gate.h` 保证旧遮罩的在途DMA、任意中间块和仅已提交的末块都不能确认新的表盘请求。
`display_init` 在 `lvgl_port_add_disp` 内部解锁之后重新取得LVGL锁，再注册自有回调、修改刷新函数与事件列表，避免与运行中的渲染任务并发修改。
这是代码中已确认的缺少同步；目前无设备证据证明该竞态已在用户手表上触发。
`identity_boot_reveal` 仅隐藏保留的覆盖层；确认完成前仍禁止短按解锁/省电；失败提示会恢复覆盖层。
诊断日志增加 `startup stage=UI begin / UI created / home frame`，配合已有复位原因、面板/触摸、core/network/OTA结果定位早期故障。
本地用LVGL9.5，发布CI实际解到9.6.0~1、esp_lvgl_port2.9.0、CO5300驱动2.2.0；已核对官方同版源码，未发现足够证据把版本差异认定为根因，未猜测性降级。
9.6兼容构建仅容许已知旧API的deprecated警告，保持其他编译警告和行为断言；未迁移无关UI。
### 设备续查（2026-10-08）

数据线直连后识别到 `/dev/cu.usbmodem2101`，ESP32-S3 rev0.2，MAC `a4:cb:8f:d6:35:b8`；
实际Flash ID `0x1940c8` 为32MiB，不应按历史容量印象设置ROM读取参数。
最初从 `ota_1` 运行原发布beta.29；读出otadata：序号11对应beta.30，状态ABORTED，CRC有效；序号10对应beta.29，状态VALID。
这证明首启未确认且已回退，不能判断是卡死、panic、掉电还是手动复位导致未确认。
用户补充：白屏持续，手动重启才恢复。beta.30完整应用区ROM MD5与归档发布包一致，未发现写入数据差异。

保存原两份otadata后，仅将 `0xf000` 的序号11状态临时改为NEW，保留beta.29的VALID选择与所有应用数据。
USB复位后beta.30在约7秒核心检查OK并确认、约10.5秒联网检查成功，未见panic/看门狗；用户确认Logo后正常进入表盘。
因此本次USB复位**未复现原故障**，不能将它当作OTA软件重启回归通过。
修订候选补全USB/JTAG等复位原因日志，避免这次 `USB_UART_CHIP_RESET` 被旧数组记为 `?`；构建已通过。
串口日志、分区表、应用头、otadata和写入读回记录在 `build/flash-records/ota-white-screen-20261008/device-20261008/`，不纳入Git。

随后真实OTA beta.30→beta.29：下载完成日志在166622ms，`RTC_SW_CPU_RST`/`last reset: sw`，从ota_1启动beta.29，用户确认正常进入表盘。
恢复原otadata的尝试因备用选择已变化而停止，未写入恢复扇区；不能将计划名/日志文件名当作恢复成功。
确认运行beta.29为VALID、完整ROM MD5匹配原包后，将本地修订候选写入备用ota_0，选择序号13/NEW。
写入MD5通过；beta.29完整MD5、分区表和NVS摘要校验未变。候选版本 `v1.7-beta.30-4-g469513b-dirty`，不是正式发布标签。
USB首启日志：3031ms UI初建、7045ms核心OK、11188ms表盘frame=1/completed=1、11221ms OTA确认，未见panic/看门狗。
用户确认本地候选和正式依赖候选均能显示表盘、解锁、进入设置。正式依赖候选版本 `v1.7-beta.30-5-g61cc58d`，
ROM写入MD5和beta.29恢复槽、NVS摘要、分区表校验通过；7071ms核心OK、11029ms表盘末块完成、11063ms OTA确认。
首轮CI的两个测试夹具单行if写法触发GCC缩进警告；只调整换行，未降低警告或断言，本机GCC15复核9/9通过。
[beta.31发布CI](https://github.com/soBigRice/soRound_os/actions/runs/37776410861)全部通过，准确bin/ELF/map/config已归档；最终发布包真实OTA验证待继续。
不得将beta.30历史CI/下载成功、本次host结果或这次USB启动成功写成白屏已解决。

### 当前续接边界（2026-10-08 20:41）

beta.31已公开发布，固件/CI/下载/官网检查通过；候选USB首启、表盘、解锁与设置由用户确认。
串口随后报Device not configured并在finally关闭；重新枚举未发现Espressif USB设备。
最终日志仍为 `v1.7-beta.30-5-g61cc58d`，没有beta.31的OTA开始、软件重启或首启记录，不能把候选确认扩写为最终包OTA验收。
下一步：重新确认本设备USB身份，开启连续采集，再由用户从系统更新安装beta.31；记录软件复位、
core/home/OTA确认顺序，并在更新完成后核对镜像MD5和VALID状态，确认表盘/解锁/设置。
不要在下载期间进入ROM或复位；先保存当前otadata，不复用旧NEW选择扇区。
源修复 `61cc58d`；GCC夹具修复 `2aef1e0`，仅用一轮修正后通过；未添加猜测性驱动补丁。
白屏具体触发点仍未复现；再次发生时先保留首启日志并使用对应发布ELF定位，不能仅增加延时。
