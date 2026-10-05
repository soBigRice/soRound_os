# soRound OS（GeekTool）

面向 **ESP32-S3-Touch-AMOLED-1.75C** 圆形 AMOLED 开发板的轻量多功能系统。项目以
ESP-IDF 固件为主线，提供圆屏启动器、表盘与锁屏、网络与传感器工具、音频、小游戏、
BLE 数字孪生以及双分区云 OTA。

当前仓库 `main` 正在准备 `v1.7-beta.23` 内测实现，最近稳定版 Tag 为 `v1.6.1`。主线开发请使用
`GeekTool-IDF/`；Arduino 工程主要用于早期原型和硬件压力测试。
beta.22 已发布至 GitHub/R2/国内 OTA；三个地址完整包与摘要一致，Range断点续传核对通过。
答案之书联网翻页更新内容，屏幕与右上PWR计时键均可翻页；断网/失败时提供明确标记的备用答案。
beta.20/21候选均在发布前取消，原tag保留。发布包适配两个4MiB OTA槽，真机联网/按键体验待验收。

## 功能概览

- 466 x 466 圆形 AMOLED UI，以黑/白/红点阵为主，天气图标使用克制配色，基于 LVGL 9。
- 径向启动器、快捷面板、锁屏表盘、亮度/音量/常显模式与 NVS 持久化。
- 19 个内置 App：Wi-Fi、I2C、系统信息、天气、日历、倒计时、秒表、设置、OTA、
  音频可视化、水平仪、迷宫、流体、骰子、BLE 遥控台（鼠标 / 演示 / 媒体）、数字孪生、答案之书、星座运势和木鱼。
- AXP2101 电源与电量管理，QMI8658 IMU，ES7210 麦克风输入和 ES8311 音频输出。
- Wi-Fi 自动重连、SNTP 校时、Open-Meteo 天气数据。
- 双 OTA 分区、启动回滚保护、版本比对和 Cloudflare R2 固件分发。
- React + Three.js Web 校准器，通过 Web Bluetooth 显示设备姿态与电量；3D 场景独立异步加载，
  蓝牙连接中途失败时会主动清理残留 GATT 会话。

## 硬件基线

| 模块 | 当前配置 |
| --- | --- |
| 主控 | ESP32-S3R8，双核 240 MHz |
| 屏幕 | 1.75 英寸、466 x 466 AMOLED、CO5300 QSPI |
| 触摸 | CST9217，I2C |
| 存储 | 32 MB Flash、8 MB OPI PSRAM |
| 传感器 | QMI8658 六轴 IMU |
| 电源 | AXP2101 PMU |
| 音频 | ES7210 麦克风 ADC、ES8311 DAC/扬声器 |
| 无线 | 2.4 GHz Wi-Fi、Bluetooth LE |

完整引脚、依赖版本和硬件说明见
[ESP32-S3-Touch-AMOLED-1.75C 开发指南](./ESP32-S3-Touch-AMOLED-1.75C-开发指南.md)。

## 目录结构

| 路径 | 用途 | 定位 |
| --- | --- | --- |
| `GeekTool-IDF/` | ESP-IDF + LVGL 9 主线固件 | 当前主线 |
| `GeekTool-IDF/main/` | 启动器、系统服务、硬件驱动和各 App | 主要开发目录 |
| `GeekTool-IDF/images/` | 随固件写入 FAT 分区的表盘图片 | 固件资源 |
| `web/` | GeekTwin React/Three.js Web Bluetooth 校准器 | 配套前端 |
| `GeekTool/` | Arduino + LVGL 8 多工具原型 | 早期版本 |
| `WiFiList_StressTest/` | Arduino 圆屏 Wi-Fi 列表压力测试 | 硬件验证 |
| `.github/workflows/firmware.yml` | Tag 构建、GitHub Release 和 R2 上传 | 发布流程 |

流畅性调度、动画和资源生命周期的当前调用链及验证方式见
[性能实现记录](./GeekTool-IDF/PORTING_NOTES.md#2026-10-01-流畅性与动画优化)。
音频点阵能量渐变、水平仪靶盘/读数分区及原生渲染回归见
[音频与水平仪设计实现](./GeekTool-IDF/AUDIO_LEVEL_DESIGN.md)。
抛硬币的双层币缘、中文大字、上抛翻面、暂停/退出及骰子回归见
[硬币实现记录](./GeekTool-IDF/PORTING_NOTES.md#2026-10-04-抛硬币外观与抛掷动画)。
系统信息三页布局、内存统计口径、刷新与字体容量防线见
[系统信息 UI](./GeekTool-IDF/SYSTEM_UI.md)。
启动器19个几何图标、原生圆屏预览及切换绘制逻辑见
[启动器图标](./GeekTool-IDF/LAUNCHER_ICONS.md)。
答案之书的联网中英文答案、断网备用、实体键翻页与生命周期见
[答案之书](./GeekTool-IDF/ANSWERS_UI.md)。
星座每日联网数据、分类长文、木鱼累计计数/声音与生命周期见
[星座与木鱼](./GeekTool-IDF/ZODIAC_MERIT_UI.md)。
设置分类导航、Wi-Fi连接/密码页及真实数据/事件边界见
[设置与Wi-Fi交互](./GeekTool-IDF/SETTINGS_LOCATION_DESIGN.md)。
OTA 下载恢复、状态同步和故障排查见
[OTA 实现记录](./GeekTool-IDF/PORTING_NOTES.md#2026-10-02-ota-失败恢复修复)。
R2 保留发布包、服务器拉取并原子替换镜像的同步程序、部署配置及迁移状态见
[OTA 服务器镜像](./GeekTool-IDF/tools/ota_mirror/README.md)。设备域名已切换到指定服务器直连。
当前 OTA 页使用整屏点阵环、大点阵箭头向上动效和按状态配色的真实下载进度，布局与设置入口见
[极简 OTA 页面](./GeekTool-IDF/PORTING_NOTES.md#2026-10-03-ota-极简页面)。
实体按键映射与验证见
[按键实现记录](./GeekTool-IDF/PORTING_NOTES.md#2026-10-02-实体按键功能对调)。
遥控台的 HID 报文、输入释放和验收见
[遥控台实现记录](./GeekTool-IDF/PORTING_NOTES.md#遥控台扩展鼠标--演示--媒体)。
天气页的彩色点阵图标、昼夜切换、布局及主机渲染验证见
[天气 UI 实现说明](./GeekTool-IDF/WEATHER_UI.md)。

## 系统关系

```mermaid
flowchart LR
    U["触摸 / 侧键"] --> L["LVGL 启动器与系统 App"]
    L --> H["显示 / 触摸 / PMU / IMU / 音频"]
    H --> B["ESP32-S3 圆屏设备"]
    B -->|"BLE GATT 传感器帧"| W["Web GeekTwin 校准器"]
    T["Git v* Tag"] --> A["GitHub Actions 构建"]
    A --> R["GitHub Release"]
    A --> C["Cloudflare R2 / GeekTool.bin"]
    C -->|"HTTPS OTA"| B
```

## 快速开始：主线固件

### 1. 准备环境

- 推荐 ESP-IDF `v6.0.1`；组件清单声明的最低版本为 `v5.1`。
- macOS 推荐使用 Espressif IDF 插件终端，或手动加载 `export.sh`。
- 首次构建会通过 ESP-IDF Component Manager 下载 LVGL、CO5300、CST9217、
  `esp_codec_dev` 等依赖，需要能够访问组件仓库。

本机使用自定义安装路径时，请把下面路径替换为实际的 `IDF_PATH`：

```bash
export PATH="/opt/homebrew/bin:$PATH"
source "$HOME/.espressif/v6.0.1/esp-idf/export.sh"
idf.py --version
```

### 2. 构建

在仓库根目录执行：

```bash
idf.py -C GeekTool-IDF set-target esp32s3
idf.py -C GeekTool-IDF build
```

成功后主固件位于：

```text
GeekTool-IDF/build/GeekTool.bin
```

### 3. 烧录与串口日志

先确认设备串口。macOS 可使用：

```bash
ls -la /dev/cu.usbmodem*
```

将 `<SERIAL_PORT>` 替换为实际串口：

```bash
idf.py -C GeekTool-IDF -p <SERIAL_PORT> flash monitor
```

例如本项目曾验证使用 `/dev/cu.usbmodem1101`。不同设备或重新插拔后端口可能变化，不应把该值
写死到脚本中。退出串口监视器使用 `Ctrl+]`。

如果烧录一直等待同步，可按住 BOOT 键后重新插入 USB 或复位，再重试烧录。

## 启动 Web 数字孪生校准器

要求安装 Node.js 和 npm。首次运行：

```bash
npm --prefix web ci
npm --prefix web run dev
```

Vite 默认尝试使用 `5173` 端口；该端口被占用时会自动选择下一个可用端口，请以终端输出的 URL
为准。Web Bluetooth 需要安全上下文，开发时使用 `http://localhost`，正式部署使用 HTTPS；推荐
桌面版 Chrome 或 Edge。

连接步骤：

1. 在设备启动器中进入 `twin` App。
2. 在浏览器打开校准器并点击“连接”。
3. 在蓝牙选择器中选择 `GeekTwin`。
4. 连接后可查看姿态、电量、数据频率，并将当前姿态设为零位。

生产构建与本地预览：

```bash
npm --prefix web run build
npm --prefix web run preview
```

更多坐标映射与 BLE 帧说明见 [web/README.md](./web/README.md)。

## 蓝牙遥控台

1. 进入 `remote` / “遥控台”，在电脑蓝牙设置中配对 `soRound`。
2. 鼠标模式：拖动触控区域移动光标，轻点左击；`L` / `R` 是左右键，右侧竖条是滚轮。
   点“拖拽”锁住左键后可用单指移动，再点一次释放。
3. 演示模式：上一页 / 下一页发送 PageUp / PageDown，需要先让电脑上的幻灯片或文档获得焦点。
   点时间开始、暂停、继续本地计时；“归零”停止并复位。计时不会启动电脑上的演示。
4. 媒体模式：播放 / 暂停、上一首 / 下一首、系统音量增减、静音。实际响应由主机和当前播放器决定。

三个模式共用一次配对和连接。只有主机完成加密并订阅对应输入报文后，该模式的控制才启用。
演示计时无需蓝牙；媒体页面不显示未经主机反馈的曲名、播放状态或音量数值。
锁屏、快捷面板遮挡和模式切换会取消未发送动作并释放按键；退出遥控台断开 BLE。

从旧版鼠标固件升级后，若鼠标能用而演示 / 媒体一直等待或无响应，先在主机删除旧的 `soRound`
配对，再重新配对，避免主机缓存旧的 HID Report Map。当前自动验证覆盖协议及真实 LVGL 页面，
电脑配对、应用响应和物理触摸仍需开发板验收。

## 固件发布与 OTA（正式 / 内测双通道）

`.github/workflows/firmware.yml` 在推送 `v*` Tag 时执行以下流程：

1. 使用 ESP-IDF `v6.0.1` 构建 `GeekTool-IDF`。
2. 把 `GeekTool.bin` 上传到同名 GitHub Release（beta Tag 自动标记 prerelease）。
3. 按 Tag 类型覆盖上传 Cloudflare R2 对象（`Cache-Control: no-store`，覆盖即"删除旧包"，
   R2 不堆积历史；历史归档在 GitHub Release）。

双通道规则：

| Tag 形式 | R2 覆盖对象 | 谁会收到 |
| --- | --- | --- |
| 正式 `v1.6` | `GeekTool.bin` **和** `GeekTool-beta.bin` | 所有设备 |
| 内测 `v1.6-beta.1` | 仅 `GeekTool-beta.bin` | 仅开了 beta 开关的设备 |

设备端点击 OTA 页右上角设置按钮，可进入 `beta` / `测试通道` 开关（存 NVS，默认关）；
返回键或右滑先返回 OTA 主界面。下载期间可以查看设置，但通道开关禁用：

- 关 → 拉 `https://ota.miaozong.cc/GeekTool.bin`，只收正式版。
- 开 → 拉 `https://ota.miaozong.cc/GeekTool-beta.bin`，收内测版；因正式版同时覆盖
  beta 对象，内测设备也不会漏掉正式更新。无需设备端比较版本新旧，不存在通道乒乓。

设备上的"旧固件"位于另一个 OTA 分区（A/B 双分区），是启动回滚的保险，
下次 OTA 自动覆盖，**不需要也不应该手动删除**。

当前客户端对临时连接失败和断流最多尝试 3 次；有强 ETag 时在同一次任务内从已写入位置续传，
没有可靠对象身份则重新下载。镜像头、项目、大小和完整镜像验证通过后才切换启动分区。
页面显示重连次数、校验阶段和失败阶段/错误码；离开页面后下载继续。
这些客户端修复需要先安装修复后的固件才会生效，旧版若无法完成 OTA，需要一次 USB 更新。

仓库需要配置以下 GitHub Actions Secrets：

- `R2_ACCESS_KEY_ID`
- `R2_SECRET_ACCESS_KEY`

发布示例：

```bash
# 正式版
git tag v1.6 && git push origin v1.6
# 内测版
git tag v1.6-beta.1 && git push origin v1.6-beta.1
```

Tag 应指向已经完成本地构建与真机验证的干净提交。OTA 完成的判断不能只看 Actions 成功或下载
地址返回 `200`，还需要在设备上验证：

1. 旧版本可检查并升级到新版本。
2. 下载、写入、重启和新分区启动均正常。
3. 再次检查同一版本时显示 `already up to date`，不会重复刷写。
4. 串口没有 TLS、证书、分区或回滚相关错误。

## 常用验证命令

```bash
# 固件编译
idf.py -C GeekTool-IDF build

# OTA 下载恢复（真实业务代码，替换传输与 Flash 接口）
cmake -S GeekTool-IDF/tests/ota -B /tmp/geektool-ota-tests
cmake --build /tmp/geektool-ota-tests
ctest --test-dir /tmp/geektool-ota-tests --output-on-failure

# LVGL 性能、OTA 状态、实体按键、遥控台 HID 与真实页面交互
cmake -S GeekTool-IDF/tests/host -B /tmp/geektool-host-tests
cmake --build /tmp/geektool-host-tests -j 8
ctest --test-dir /tmp/geektool-host-tests --output-on-failure

# Web 类型检查 + 生产构建
npm --prefix web run build

# 检查补丁空白和冲突标记
git diff --check
```

涉及硬件交互的改动至少还应验证屏幕、触摸、启动器导航、PWR/BOOT 键、Wi-Fi、音频、IMU、
锁屏/省电和 OTA 中直接受影响的项目。仅通过编译不等于真机功能已验收。

`v1.7-beta.11` 联合发布验证（2026-10-03）：OTA 与天气改版的固件完整构建、18 组 OTA 下载恢复/错误边界、
6 组主机回归（LVGL 性能、26 个中英文 OTA 页面渲染及设置交互、实体按键、HID 协议、遥控台交互和布局、78 个天气页面渲染）通过；
本地 LVGL 9.5 与发布所用上游 9.6.0 的六组回归均通过。
随后天气原稿图标修正新增固定像素基准，本地 LVGL 9.5 的七组回归（含 64 组原稿像素比对）
与固件构建通过，并已 USB 安装本地测试镜像、确认启动及 Wi-Fi 重连。当时设置与地址选择仍为交互稿，已在 beta.13 实现。
点阵动效及状态配色的真机观感、新增遥控台功能和实际设备 OTA 下载、断线恢复与重启仍待验证。
Web 11 组回归、类型检查和生产构建为 2026-10-01 流畅性发布时的结果，本次未改 Web。

当前本地按键映射：BOOT 短按锁屏/解锁，长按 2 秒软件关机；PWR 短按控制秒表及倒计时的
开始/暂停/继续（倒计时结束后复位）。该映射此前已 USB 烧录并确认启动有效，实体按键操作待验收。
`v1.7-beta.11` 保留该按键映射与遥控台扩展，并合并 OTA / 天气页面改版；发布镜像已于
2026-10-03 USB 烧录、完整读回校验并确认启动，新页面的真机观感与触摸操作仍待验收。
断电后的 PWR 上电、按住 BOOT 上电进入下载模式属于硬件功能。

## 已知边界

- CO5300 使用 QSPI，无法获得 RGB/DSI 面板级的 tearing avoidance；大面积快速重绘仍可能出现轻微
  撕裂。当前 UI 通过小面积动画和减少全屏重绘控制影响。
- Web 校准器没有磁力计或完整姿态四元数，水平偏航只能做短时间相对估算，长时间会漂移。
- Web Bluetooth 的浏览器支持有限；Safari 和 Firefox 不能作为当前校准器的主要运行环境。
- `GeekTool-IDF/PORTING_NOTES.md` 是按开发时间累积的记录，早期章节描述的是当时状态；当前入口、
  构建和发布方式以本 README、代码和工作流配置为准。

## 开发文档

- [ESP-IDF 移植、真机记录与历史问题](./GeekTool-IDF/PORTING_NOTES.md)
- [音频与水平仪确认稿实现](./GeekTool-IDF/AUDIO_LEVEL_DESIGN.md)
- [天气首屏保护、详情数据与原生滚动](./GeekTool-IDF/WEATHER_DETAILS_DESIGN.md)
- [Web GeekTwin 校准器说明](./web/README.md)
- [Arduino GeekTool 原型说明](./GeekTool/README.md)
- [Arduino Wi-Fi 列表压力测试](./WiFiList_StressTest/README.md)
- [硬件开发指南](./ESP32-S3-Touch-AMOLED-1.75C-开发指南.md)

提交功能改动时，请同步更新直接相关的代码注释、模块文档和本 README 中受影响的状态、命令或
验收说明，避免文档与真实固件行为分离。

2026-10-04 已发布 [v1.7-beta.13](https://github.com/soBigRice/soRound_os/releases/tag/v1.7-beta.13)：
新增单屏设置交互与离线省市区天气地址选择，补齐夜间降水点阵云形、IMU 断读/停止采样恢复。
LVGL 9.5/9.6 的十组主机回归、固件构建与公开 beta 镜像校验通过，正式通道保持原镜像。
用户选择自行 OTA；本轮未执行 USB 写入，真机侧倾与新交互待验收。调用链和数据覆盖范围见
[设置与地址说明](GeekTool-IDF/SETTINGS_LOCATION_DESIGN.md)及
[发布验证记录](GeekTool-IDF/PORTING_NOTES.md#2026-10-04-v17-beta13-发布核对)。

2026-10-04 已发布 [v1.7-beta.14](https://github.com/soBigRice/soRound_os/releases/tag/v1.7-beta.14)：
音频能量渐变与水平仪靶盘按确认稿实现，修正读数字形与刻度穿透；天气保持原首屏，
增加当前详情、12 小时趋势、五日预报、日光/UV、惯性滚动与右侧圆弧滚动条。
LVGL 9.5/9.6 各十一组回归通过，天气字库无损裁去透明留白；发布包适配现有 3 MiB OTA 槽。
GitHub、R2 与国内 beta 镜像逐字节一致，完整/断点下载通过，正式通道仍为 `v1.6.1`。
设备 OTA 设置开启测试通道后检查更新；本轮未执行刷机，实际圆屏体验仍待设备验收。
源码提交 `9680430`，容量、摘要与发布证据见
[发布验证记录](GeekTool-IDF/PORTING_NOTES.md#2026-10-04-v17-beta14-发布核对)。

2026-10-04 已发布 [v1.7-beta.15](https://github.com/soBigRice/soRound_os/releases/tag/v1.7-beta.15)：
修复硬币中文正反字样显示方框，新增双层币缘、整枚上抛/翻面/落地回弹；保留点击、甩动与骰子模式。
LVGL 9.6 全部十二组主机回归、十八项 OTA 恢复边界及发布构建通过；GitHub、R2 和设备 beta
下载地址逐字节一致，完整/断点下载与镜像校验通过。设备 OTA 设置开启测试通道后升级。
源码提交 `5d98a06`，真实升级/重启与动画手感待设备验收；摘要和容量见
[发布验证记录](GeekTool-IDF/PORTING_NOTES.md#2026-10-04-v17-beta15-发布核对)。

2026-10-04 已发布 [v1.7-beta.16](https://github.com/soBigRice/soRound_os/releases/tag/v1.7-beta.16)：
修复天气详情查询超出默认 HTTP 发送缓冲、导致已联网时所有城市获取失败的问题；接口失败与断网提示分开。
LVGL 9.5/9.6 各十二组回归和发布构建通过，原首屏、图标和字形检查保持一致。
GitHub、R2、国内 beta 包逐字节一致，镜像校验与断点下载通过。
用户随后确认 USB 重启后天气已正常，原失败原因仍待请求日志定位。源码 `7451829`，证据见
[发布验证记录](GeekTool-IDF/PORTING_NOTES.md#2026-10-04-v17-beta16-发布核对)。

2026-10-04 已发布 [v1.7-beta.17](https://github.com/soBigRice/soRound_os/releases/tag/v1.7-beta.17)：
系统信息新增总览、内存、设备三页，实时显示 RAM/PSRAM 已用与可用、连续空闲块、
芯片容量、固件/SDK、运行时长及任务数。LVGL 9.5/9.6 各十四组回归通过。
当前设备已按用户授权跳过备份，经 USB 迁移至双4MiB OTA槽，设置区域摘要未变，
发布镜像启动、外设及原 Wi-Fi 重连通过；系统页实际显示与触摸仍待用户验收。
GitHub/R2/国内包全文摘要一致，国内两次整包请求超时后用断点续传完成校验，正式通道仍为 `v1.6.1`。
现有3MiB布局可OTA安装此包，分区扩容本身需USB迁移。逻辑见 [SYSTEM_UI](GeekTool-IDF/SYSTEM_UI.md)，
完整证据见 [发布记录](GeekTool-IDF/PORTING_NOTES.md#2026-10-04-v17-beta17-发布与usb迁移核对)。

2026-10-04 已发布 [v1.7-beta.18](https://github.com/soBigRice/soRound_os/releases/tag/v1.7-beta.18)：
重绘启动器16个App图标和切换箭头，统一圆润白色线条、红色语义细节并减淡按钮边框；
保留应用顺序、点击/滑动和220ms切换。LVGL9.5/9.6各14组回归及发布构建通过，
中英文466×466原生预览已检查。GitHub/R2/国内包完整摘要一致，国内首次连接重置后分段续传校验通过，
正式通道仍为 `v1.6.1`。USB未连接，本次未刷写设备；真实观感、触摸与设备OTA升级重启待验收。
图标逻辑见 [LAUNCHER_ICONS](GeekTool-IDF/LAUNCHER_ICONS.md)，完整证据见
[发布记录](GeekTool-IDF/PORTING_NOTES.md#2026-10-04-v17-beta18-图标发布核对)。

2026-10-05 已发布 [v1.7-beta.19](https://github.com/soBigRice/soRound_os/releases/tag/v1.7-beta.19) 至GitHub/R2/国内OTA：
图标恢复2px白色外圈、名字24px；设置改为四分类及逐级返回，Wi-Fi分离连接状态、附近网络与独立密码输入。
LVGL9.5/9.6各15组回归及发布构建通过；发布包3,175,440B，须使用双4MiB布局（当前设备此前已迁移）。
GitHub/R2/国内整包与摘要一致，两个OTA地址的条件Range验证通过；镜像程序旧3MiB限制已修正并部署，
13组镜像回归通过。两个地址正式包全文摘要仍为v1.6.1，正式通道保持不变。
USB未连接，本次未烧录；真实观感、设置/密码页触摸、无线连接及设备OTA重启待验收。
调用链见 [设置与Wi-Fi](GeekTool-IDF/SETTINGS_LOCATION_DESIGN.md)，状态见
[发布记录](GeekTool-IDF/PORTING_NOTES.md#2026-10-05-v17-beta19-发布与镜像上限核对)。
