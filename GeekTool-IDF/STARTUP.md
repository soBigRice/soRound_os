# 启动放行与 OTA 首启自检

核对日期：2026-10-07；本地主机验证为ESP-IDF 6.0.1、LVGL 9.5，初次验证基线 `b626cbd`；实现随 `v1.7-beta.30 / 5efc052` 发布。
用户已确认：至少4秒动画、初始化完成才放行；联网失败记录并提示，允许离线启动；完成后进入原有锁屏表盘。
本地与发布验证已完成，[CI/R2/国内OTA结果](./PORTING_NOTES.md#2026-10-07-v17-beta30-设置联网与启动自检发布)已核对；真实设备启动、OTA与回滚仍待验收。历史本地候选和测试回执见
`build/flash-records/startup-selftest-20261007/`，当前原生预览见 [启动审阅](./artwork/identity/native/startup-20261007/README.md)。

## 入口与职责

```text
main.c:app_main
  → NVS / audio_bus / I2C / IMU / RTC / display / settings / i18n / touch
  → LVGL锁内：identity_boot_create → launcher_start → 覆盖层移至最前
  → 锁外：图片后台预热 → wifi_service_start
  → startup_selftest：核心资源 + 图片任务完成 + 活跃心跳/首帧DMA + 至少4秒
  → startup_apply_ota_result：仅处理运行槽的PENDING_VERIFY
  → startup_network_check：IP就绪时，对选定OTA入口做一次HTTPS HEAD
  → 联网失败：日志 + 中英文离线提示1.2秒
  → LVGL锁内：刷新已选表盘 → identity_boot_release → 原有锁屏表盘
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
  核心检查通过 + 至少4秒 → mark_app_valid_cancel_rollback → 可选联网检查 → 表盘
  核心检查失败 → 有可回退镜像时 mark_app_invalid_rollback_and_reboot
```

| OTA状态或结果 | 处理 |
| --- | --- |
| 普通/VALID启动 | 不确认、不标记无效；核心失败保留错误覆盖层，不主动进入重启循环。 |
| 直接烧录、运行槽没有otadata条目 | SDK返回`ESP_ERR_NOT_FOUND`时允许普通启动，不伪造OTA状态。 |
| PENDING_VERIFY核心通过 | 确认有效；联网检查在此后执行，服务端/路由/无网故障不触发固件回滚。 |
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

29组主机测试通过，使用实际LVGL渲染/调度与生产自检/探测代码，SDK及硬件边界为夹具；ESP-IDF固件构建通过。
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
