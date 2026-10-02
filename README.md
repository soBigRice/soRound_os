# soRound OS（GeekTool）

面向 **ESP32-S3-Touch-AMOLED-1.75C** 圆形 AMOLED 开发板的轻量多功能系统。项目以
ESP-IDF 固件为主线，提供圆屏启动器、表盘与锁屏、网络与传感器工具、音频、小游戏、
BLE 数字孪生以及双分区云 OTA。

当前仓库 `main` 对应 `v1.7-beta.10` 内测版本，最近稳定版 Tag 为 `v1.6.1`。主线开发请使用
`GeekTool-IDF/`；Arduino 工程主要用于早期原型和硬件压力测试。

## 功能概览

- 466 x 466 圆形 AMOLED UI，黑/白/红点阵视觉，基于 LVGL 9。
- 径向启动器、快捷面板、锁屏表盘、亮度/音量/常显模式与 NVS 持久化。
- 16 个内置 App：Wi-Fi、I2C、系统信息、天气、日历、倒计时、秒表、设置、OTA、
  音频可视化、水平仪、迷宫、流体、骰子、BLE 遥控台（鼠标 / 演示 / 媒体）和数字孪生。
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
OTA 下载恢复、状态同步和故障排查见
[OTA 实现记录](./GeekTool-IDF/PORTING_NOTES.md#2026-10-02-ota-失败恢复修复)。
实体按键映射与验证见
[按键实现记录](./GeekTool-IDF/PORTING_NOTES.md#2026-10-02-实体按键功能对调)。
遥控台的 HID 报文、输入释放和验收见
[遥控台实现记录](./GeekTool-IDF/PORTING_NOTES.md#遥控台扩展鼠标--演示--媒体)。

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

设备端在 OTA 页有 `beta` / `测试通道` 开关（存 NVS，默认关）：

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

最近一次本地验证（2026-10-03）：遥控台扩展后的固件完整构建、18 组 OTA 下载恢复/错误边界、
5 组主机回归（LVGL 性能、8 个中英文 OTA 状态、实体按键、HID 协议、遥控台交互和布局）通过；
新增遥控台功能及实际设备 OTA 下载、断线恢复和重启仍待验证。
Web 11 组回归、类型检查和生产构建为 2026-10-01 流畅性发布时的结果，本次未改 Web。

当前本地按键映射：BOOT 短按锁屏/解锁，长按 2 秒软件关机；PWR 短按控制秒表及倒计时的
开始/暂停/继续（倒计时结束后复位）。本地版本已 USB 烧录并确认启动有效，实体按键操作待验收。
`v1.7-beta.10` 同时包含该按键映射与遥控台扩展。
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
- [Web GeekTwin 校准器说明](./web/README.md)
- [Arduino GeekTool 原型说明](./GeekTool/README.md)
- [Arduino Wi-Fi 列表压力测试](./WiFiList_StressTest/README.md)
- [硬件开发指南](./ESP32-S3-Touch-AMOLED-1.75C-开发指南.md)

提交功能改动时，请同步更新直接相关的代码注释、模块文档和本 README 中受影响的状态、命令或
验收说明，避免文档与真实固件行为分离。
