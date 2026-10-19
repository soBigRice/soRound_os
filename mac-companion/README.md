# soRound 本地媒体桥接 / Local media bridge

2026-10-19：本地实现与协议测试完成；未烧录、未验证真实 BLE 封面吞吐。Mac 27.0.1 上对私有接口执行一次只读 `get` 返回成功但当前无 Now Playing 数据，不能据此宣称所有播放器已兼容。

## 运行 / Run

```sh
./mac-companion/build.sh
./mac-companion/run.command
# 可显式指定 CoreBluetooth UUID；不使用 MAC 地址。
# Optionally select the CoreBluetooth UUID, not a MAC address.
./mac-companion/run.command --device 00000000-0000-0000-0000-000000000000
# 本地 UTF-8 LRC 必须显式绑定系统曲名；切到其他标题时不使用它。
# A local UTF-8 LRC must explicitly match the system song title.
./mac-companion/run.command --lrc /absolute/song.lrc --lrc-title 'Exact song title'
```

先在设备打开遥控台，在 Mac 蓝牙设置配对 `soRound`。桥接首次使用需要蓝牙权限。`run.command` 保持前台，Ctrl-C 停止；不要把 App 包加入登录项。没有同名唯一设备或无法访问加密服务时停止并提供诊断，不自动重连，不自动删配对。若系统缓存旧服务、提示找不到 media GATT，先核对固件版本再由用户手动重新配对。

三键控制继续由设备标准 BLE HID 完成。工具不发送媒体控制命令，不模拟系统按键，不申请辅助功能权限，不读取麦克风，不访问网络，不搜索歌词，不自动启动。曲目元数据和本地渲染图只经加密 BLE 发给当前 HID 会话；不写歌曲日志或持久封面缓存。BLE 读取速度、封面传输耗时和不同播放器公开字段仍需实测。

桥接退出后，系统 HID 可能仍保持物理连接；固件最多 8 秒未收到有效元数据心跳即清空桥接数据。设备退出遥控台或 HID 断开时立即清空；重新进入后手动重启桥接。

## 实现链路 / Implementation

`Sources/main.swift: MediaBridge.startSource()` → 系统 `/usr/bin/perl` → 固定版本 `mediaremote-adapter` 的 `stream --debounce=100` → JSON diff 合并 → CoreText / ImageIO 本地排版 → CoreBluetooth 独立 media GATT → `ble_media.c` → `remote_media.c` → UI revision 校验复制到自有 PSRAM。

- 封面完整 **466×466、小端 RGB565（434312 字节）**。Mac 等比例放入黑色正方形画布，不裁掉非正方形封面的内容。
- 文本 **350×180 A8（63000 字节）**，白色 alpha 遮罩。有效本地歌词为三行、当前句大字，底部曲名歌手；无歌词时居中曲名歌手，并明确显示未提供本地歌词。CoreText 完成中文字形回退，固件不依赖小字库。
- 两层独立 staging，逐片 `write withResponse`，交错传输；换曲更换 generation 并取消两层旧任务。歌词换行仅替换 overlay，封面继续传输。传输上限、连续 offset、总长度及 CRC32 全部由固件验证，完整后一次发布；UI 不读取半张图。
- 没封面时保留 UI 的默认背景；换曲时清除上一曲封面。所有字段以当前源 App 实际发布内容为准，不推断“所有 App 均有封面/歌词”。

## 协议 v1 / Wire protocol

服务 `73520001-6e93-4fd8-bf40-2b54f4aa4f47`；RX `73520002-...`（WRITE + WRITE_ENC）；STATUS `73520003-...`（READ + READ_ENC）。另外校验当前 HID connection handle、实际加密状态及遥控台可见性。每包最多 512 字节。整数均为小端。

公共头为 16 字节：`"SR"(2) | version=1(1) | opcode(1) | generation:u32 | offset:u32 | value:u32`。

| opcode | offset / value | payload | 行为 |
| --- | --- | --- | --- |
| 1 META | 0 / 0 | flags:u32、elapsed_ms:u32、duration_ms:u32 | flags bit0 playing、bit1 本地歌词匹配；新 generation 清空两图层。1 秒心跳；同会话拒绝倒退代次 |
| 2 BEGIN | kind / 固定图层字节数 | crc32:u32 | kind 1 封面、2 遮罩；取消该层旧 staging |
| 3 DATA | 连续字节 offset / kind | 原始字节 | 单次大小按 CoreBluetooth 上限取最小值；逐片 ACK |
| 4 COMMIT | kind / 0 | 无 | 长度与 CRC 匹配后发布 |
| 5 CLEAR | kind 或 0=两层 / 0 | 无 | 清该层已发布图与 staging |
| 6 RESET | 0 / 0，generation=0 | 无 | 手动新桥接会话清空 |

STATUS 为 20 字节：`"SR" | version=1 | connected:u8 | generation:u32 | artwork_offset:u32 | overlay_offset:u32 | revision:u32`。

每层 5 秒无数据或总传输超过 120 秒即取消 staging；8 秒无有效 META 则清空全部。最大接收缓存为两层 committed + 两层 staging（约 972 KiB），另有 UI 自有两层 buffer（约 486 KiB）。实际分配使用 PSRAM，内存不足拒绝 BEGIN；不会降画质或悄悄截断图层。

## 验证 / Verification

```sh
./GeekTool-IDF/tests/remote_media/run.sh
'./mac-companion/dist/soRound Media.app/Contents/MacOS/soRoundMedia' --self-test
```

协议测试编译真实 `remote_media.c` / `ble_media.c`，替换硬件 API；ASan+UBSan 覆盖加密和连接隔离、包边界、偏移、两图层交错、CRC、原子复制、旧代次、无封面/断连清空、超时与分配失败。Mac 自测覆盖共享 CRC golden vector、包编码、LRC 时间戳、CoreText 中文遮罩、ImageIO PNG 到完整 RGB565 的已知颜色转换及无效输入。`--self-test --fixtures /absolute/dir` 可输出明确合成的渐变封面与中文遮罩供原生 UI 检查；不访问真实媒体或蓝牙。

真实设备仍需检查：首次蓝牙授权、既有 HID 同连接读写、434312 字节封面的耗时、音乐切换取消、歌词持续换行是否完成、电脑播放器分别是否提供封面/进度、暂停/继续旋转、退出与重新进入。未连接硬件前不能标记这些通过。

## 选择与代价 / Decision

固定 BSD-3-Clause 上游提交见 `vendor/mediaremote-adapter/UPSTREAM.txt`，保留原许可。原始 adapter 能流式输出并处理封面短暂丢失；没有引入 GPL-3.0 的完整 nowplaying-cli，没有 Homebrew/pip/npm 运行时。使用系统 Foundation/AppKit/CoreBluetooth/CoreText/ImageIO 和 CMake。构建脚本只在临时目录编译，复制必要 App 产物到 `dist/`，使用 ad-hoc 签名并清理临时文件；不安装证书、系统扩展或服务。

代价是私有 MediaRemote 及系统 Perl 的加载路径可能在 macOS 更新后失效，普通签名不等于获得公开权限。此方案用于明确授权的本地手动工具，不承诺 App Store 发布。没有改 SIP、注入系统守护进程或申请 Apple 私有 entitlement。局域网路径会增加网络发现、端口和认证，因此本次采用加密 BLE；真实吞吐不达要求时需据测量另行讨论，不能暗中降低清晰度。

核查来源（2026-10-19）：[Apple Now Playing 发布接口](https://developer.apple.com/documentation/mediaplayer/mpnowplayinginfocenter)、[adapter 机制与限制](https://github.com/ungive/mediaremote-adapter)、[CoreBluetooth 当前连接](https://developer.apple.com/documentation/corebluetooth/cbcentralmanager/retrieveconnectedperipherals(withservices:))、[写入上限](https://developer.apple.com/documentation/corebluetooth/cbperipheral/maximumwritevaluelength(for:))、[蓝牙权限](https://developer.apple.com/documentation/BundleResources/Information-Property-List/NSBluetoothAlwaysUsageDescription)。
