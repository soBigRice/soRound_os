# ESP32-S3-Touch-AMOLED-1.75C 开发指南

> 微雪（Waveshare）1.75 寸圆形电容触摸 AMOLED 开发板 —— 关键信息提取、引脚分配与技术方案汇总
>
> 资料来源：[微雪官方文档](https://docs.waveshare.net/ESP32-S3-Touch-AMOLED-1.75C/) ｜ [示例程序仓库](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C)
> 整理日期：2026-06-18

---

## 目录

1. [产品概述](#1-产品概述)
2. [核心规格速查表](#2-核心规格速查表)
3. [主要芯片与板载外设](#3-主要芯片与板载外设)
4. [GPIO 引脚分配表（重点）](#4-gpio-引脚分配表重点)
5. [总线架构（I2C / QSPI / I2S）](#5-总线架构i2c--qspi--i2s)
6. [开发环境搭建](#6-开发环境搭建)
7. [Arduino 库依赖（含版本）](#7-arduino-库依赖含版本)
8. [官方示例程序说明](#8-官方示例程序说明)
9. [关键代码片段](#9-关键代码片段)
10. [技术方案与解决方案](#10-技术方案与解决方案)
11. [小智 AI 语音方案](#11-小智-ai-语音方案)
12. [固件烧录与恢复](#12-固件烧录与恢复)
13. [常见问题（FAQ）](#13-常见问题faq)
14. [资料下载与链接](#14-资料下载与链接)
15. [注意事项与待核实项](#15-注意事项与待核实项)

---

## 1. 产品概述

ESP32-S3-Touch-AMOLED-1.75C 是微雪设计的一款**高集成度、便携式 AIoT 开发板**，采用「电子吧唧」造型的 **CNC 铝合金外壳**，小巧美观。围绕语音 AI / 可穿戴 / 桌面智能体场景而设计：

- **1.75 寸圆形电容触摸 AMOLED 屏（466 × 466，1670 万色）**
- **高集成电源管理（AXP2101）+ 板载锂电池接口**，可脱机供电
- **六轴 IMU（QMI8658）**：姿态、计步、手势
- **双麦克风阵列 + 回声消除（ES7210 AEC）+ 音频编解码（ES8311）+ 内置扬声器焊盘**，天然适配语音唤醒 / 语音对话 AI
- 预留电池仓，适合做**便携语音助手、智能徽章、桌面 AI 伴侣**等产品原型

支持 **Arduino IDE** 和 **ESP-IDF** 两种开发框架。

---

## 2. 核心规格速查表

| 项目 | 规格 |
|------|------|
| 主控 MCU | **ESP32-S3R8**（Xtensa 32-bit LX7 双核，最高 240 MHz） |
| 内存 | 512 KB SRAM + 384 KB ROM；**叠封 8 MB PSRAM** |
| Flash | **16 MB / 32 MB NOR Flash**（文档不一致，见 [§15](#15-注意事项与待核实项)） |
| 无线 | 2.4 GHz Wi-Fi 802.11 b/g/n + Bluetooth 5 (LE)，板载贴片天线 |
| 显示屏 | 1.75″ AMOLED，**466 × 466**，1670 万色（16.7 M），圆形 |
| 显示驱动 IC | **CO5300**（QSPI 接口） |
| 触摸 IC | **CST9217 / CST92xx 系列**（I2C 接口，电容触摸） |
| IMU | **QMI8658**（3 轴加速度 + 3 轴陀螺仪，I2C） |
| 电源管理 | **AXP2101**（多路输出 + 充电 / 电池管理，I2C） |
| 音频编解码 | **ES8311**（DAC/播放，I2S + I2C 控制） |
| 回声消除 / 麦克风 ADC | **ES7210**（双麦拾音 + AEC，I2S + I2C 控制） |
| 功放使能 | PA 引脚（GPIO46） |
| 按键 | **PWR**（电源 / 自定义）、**BOOT**（下载 / 调试），均为侧边按键 |
| USB | Type-C（ESP32-S3 原生 USB，烧录 + 日志） |
| 电池 | MX1.25 2PIN 连接器，3.7 V 锂电池，支持充放电 |
| 扬声器 | 板载扬声器焊盘 |
| 外壳 | CNC 铝合金，电子吧唧造型 |

---

## 3. 主要芯片与板载外设

| 功能 | 型号 | 接口 | 说明 |
|------|------|------|------|
| 主控 SoC | ESP32-S3R8 | — | 双核 LX7 @240 MHz，叠封 8 MB PSRAM |
| 显示驱动 | CO5300 | QSPI | 466×466 AMOLED，Arduino 中用 `Arduino_CO5300` 驱动类 |
| 触摸 | CST9217（CST92xx） | I2C | Arduino 中用 SensorLib 的 `TouchDrvCST92xx` |
| 六轴传感器 | QMI8658 | I2C | 加速度 + 陀螺仪，SensorLib 驱动 |
| 实时时钟 RTC | PCF85063（待核实） | I2C | 概述提及 RTC，SensorLib 支持 PCF85063；见 [§15](#15-注意事项与待核实项) |
| 电源管理 PMU | AXP2101 | I2C | 充电管理、多路电压、电池 ADC，XPowersLib 驱动 |
| 音频 Codec | ES8311 | I2S + I2C | 音频播放（DAC），驱动扬声器 |
| 麦克风 ADC / AEC | ES7210 | I2S + I2C | 双麦拾音、回声消除算法 |
| 功放 | PA（使能脚 GPIO46） | GPIO | 控制扬声器功放通断 |

> 音频链路：**ES7210 负责"听"（麦克风 → ADC → AEC）**，**ES8311 负责"说"（DAC → 功放 → 扬声器）**，两颗芯片共用 I2S 总线，控制寄存器走 I2C。这是做语音对话 AI 的硬件基础。

---

## 4. GPIO 引脚分配表（重点）

> ⭐ 以下引脚号**取自官方示例库 `Mylibrary/pin_config.h`**（最权威来源），适用于 Arduino 与 ESP-IDF。

### 显示屏（QSPI，CO5300）

| 信号 | GPIO | 宏定义 |
|------|------|--------|
| CS（片选） | **GPIO12** | `LCD_CS` |
| SCLK（时钟） | **GPIO38** | `LCD_SCLK` |
| SDIO0 / D0 | **GPIO4** | `LCD_SDIO0` |
| SDIO1 / D1 | **GPIO5** | `LCD_SDIO1` |
| SDIO2 / D2 | **GPIO6** | `LCD_SDIO2` |
| SDIO3 / D3 | **GPIO7** | `LCD_SDIO3` |
| RESET | **GPIO2** | `LCD_RESET` |
| 分辨率 | 466 × 466 | `LCD_WIDTH` / `LCD_HEIGHT` |

### 触摸 + I2C 总线（CST9217）

| 信号 | GPIO | 宏定义 |
|------|------|--------|
| I2C SDA | **GPIO15** | `IIC_SDA` |
| I2C SCL | **GPIO14** | `IIC_SCL` |
| 触摸中断 INT | **GPIO11** | `TP_INT` |
| 触摸复位 RST | **GPIO2** | `TP_RST`（⚠ 与 `LCD_RESET` 共用 GPIO2） |

### 音频（I2S）

| 信号 | GPIO | 宏定义 | 备注 |
|------|------|--------|------|
| ES7210 BCLK | **GPIO9** | `PIN_ES7210_BCLK` | I2S 位时钟（与 ES8311 共用） |
| ES7210 LRCK / WS | **GPIO45** | `PIN_ES7210_LRCK` | I2S 帧时钟（与 ES8311 共用） |
| ES7210 DIN | **GPIO10** | `PIN_ES7210_DIN` | 麦克风数据 → ESP32 |
| ES7210 MCLK | **GPIO16** | `PIN_ES7210_MCLK` | 主时钟 |
| ES8311 DOUT | **GPIO8** | `PIN_ES8311_DOUT` | 音频数据 ESP32 → 扬声器 |
| PA 功放使能 | **GPIO46** | `PA` | 拉高使能扬声器功放 |

### 引脚占用一览（按 GPIO 排序）

```
GPIO2  → LCD_RESET / TP_RST (共用)
GPIO4  → LCD_SDIO0
GPIO5  → LCD_SDIO1
GPIO6  → LCD_SDIO2
GPIO7  → LCD_SDIO3
GPIO8  → ES8311 DOUT  (I2S 播放数据)
GPIO9  → I2S BCLK
GPIO10 → ES7210 DIN   (I2S 麦克风数据)
GPIO11 → TP_INT       (触摸中断)
GPIO12 → LCD_CS
GPIO14 → I2C SCL
GPIO15 → I2C SDA
GPIO16 → I2S MCLK
GPIO38 → LCD_SCLK
GPIO45 → I2S LRCK/WS
GPIO46 → PA (功放使能)
```

> AXP2101、QMI8658、ES8311、ES7210、CST9217（及可能的 PCF85063 RTC）的**控制接口全部挂在同一条 I2C 总线（SDA=15 / SCL=14）** 上。

---

## 5. 总线架构（I2C / QSPI / I2S）

```
                         ESP32-S3R8
        ┌───────────────────┼────────────────────┐
        │ QSPI              │ I2C (SDA15/SCL14)   │ I2S (BCLK9/LRCK45/MCLK16)
        ▼                   ▼                     ▼
   ┌─────────┐   ┌──────────────────────┐   ┌──────────────────┐
   │ CO5300  │   │ CST9217  (触摸)        │   │ ES7210 (麦克风/AEC│
   │ AMOLED  │   │ QMI8658  (IMU)         │   │   DIN=10)        │
   │ 466×466 │   │ AXP2101  (PMU)         │   │ ES8311 (扬声器    │
   └─────────┘   │ ES8311/ES7210 (控制)   │   │   DOUT=8)        │
                 │ PCF85063 (RTC, 待确认) │   └──────────────────┘
                 └──────────────────────┘
```

**典型 I2C 设备地址**（多数为固定值，建议上电后做一次 I2C 扫描确认）：

| 芯片 | 典型 7-bit 地址 |
|------|----------------|
| AXP2101 | `0x34` |
| ES8311 | `0x18` |
| QMI8658 | `0x6A` 或 `0x6B` |
| PCF85063（RTC） | `0x51` |
| ES7210 | `0x40`（也可能 0x41–0x43） |
| CST9217（触摸） | 需扫描确认 |

> 上电后用一段 I2C 扫描程序遍历 `0x01–0x7F` 打印 ACK 设备，可一次性确认全部地址。

---

## 6. 开发环境搭建

支持两种框架，按需选择：

| 框架 | 优势 | 适合人群 |
|------|------|----------|
| **Arduino IDE** | 简单易上手、库丰富、社区大 | 初学者 / 快速原型 |
| **ESP-IDF** | 控制力强、性能好（可启用双缓冲 / 双加速）、调试完善 | 进阶 / 复杂项目 / 量产 |

### Arduino IDE

1. 安装 Arduino IDE 并添加 ESP32 开发板支持（开发板管理器 → 安装 `esp32` by Espressif）。
2. **示例基于 Arduino-ESP32 core v3.3.5**（仓库目录 `examples/Arduino-v3.3.5`），建议使用相近版本以保证兼容。
3. 安装下表所列库（注意版本，见 [§7](#7-arduino-库依赖含版本)）。
4. 选择开发板：`ESP32S3 Dev Module`；常用配置：
   - **PSRAM: OPI PSRAM**（8 MB 叠封 PSRAM 必须开启）
   - **Flash Size: 16 MB（或 32 MB，以实测为准）**
   - **USB CDC On Boot: Enabled**（示例用 `USBSerial` / `HWCDC` 打印日志）
   - Partition Scheme：按需选择带较大 APP 分区的方案（如 `16M Flash (3MB APP/9.9MB FATFS)`）

### ESP-IDF（VS Code）

- 推荐 **VS Code + Espressif IDF 插件**。
- ⚠ 官方 ESP-IDF 专属教程页当前**尚未上线（404）**。在此之前，可参考：
  - 微雪通用《ESP-IDF (VS Code) 开发环境搭建教程》
  - [小智 AI 项目](https://github.com/78/xiaozhi-esp32)（基于 ESP-IDF，含本板适配）作为 IDF 工程参考
  - 示例仓库中的 ESP-IDF 工程（如有）

---

## 7. Arduino 库依赖（含版本）

> ⚠ **版本强相关**：LVGL 与各驱动库版本耦合较强（v8 驱动不兼容 v9）。请严格使用下列版本，混用易导致编译失败或运行异常。

| 库 / 文件 | 用途 | 版本 | 安装方式 |
|-----------|------|------|----------|
| **GFX Library for Arduino** | AMOLED 图形显示（CO5300） | **v1.6.4** | 库管理器 / 手动 |
| **SensorLib** | QMI8658、PCF85063、CST92xx 触摸驱动 | **v0.3.3** | 库管理器 / 手动 |
| **XPowersLib** | AXP2101 电源管理 | **v0.2.6** | 库管理器 / 手动 |
| **lvgl** | GUI 框架 | **v8.4.0** | 库管理器 / 手动 |
| **Mylibrary** | 开发板引脚宏定义（`pin_config.h`） | — | **手动安装** |
| **lv_conf.h** | LVGL 配置文件 | — | **手动安装** |

**安装步骤**：

1. 从示例程序包的 `Arduino/libraries` 目录，将全部文件夹复制到 Arduino 库目录。
2. Arduino 库目录通常为：`C:\Users\<用户名>\Documents\Arduino\libraries`（可在 IDE → 文件 → 首选项查看「项目文件夹位置」，其下的 `libraries`）。
3. `Mylibrary`（引脚宏）与 `lv_conf.h`（LVGL 配置）必须手动放置到位。

---

## 8. 官方示例程序说明

位于示例仓库 `examples/Arduino-v3.3.5/examples/`：

| 示例 | 说明 | 依赖库 |
|------|------|--------|
| **01_HelloWorld** | GFX 基础图形 / 显示屏性能测试，随机文本 | GFX |
| **02_GFX_AsciiTable** | 按行列打印 ASCII 字符表 | GFX |
| **03_LVGL_AXP2101_ADC_Data** | XPowersLib 读取 AXP2101 电源数据；PWR 键控制亮屏/熄屏 | GFX / XPowersLib |
| **04_LVGL_QMI8658_ui** | LVGL 绘制加速度折线图 + 读取 IMU | LVGL / SensorLib |
| **05_LVGL_Widgets** | LVGL Widgets 演示，动态 50–60 fps；含触摸 + IMU | LVGL / SensorLib |
| **06_ES7210** | I2S 驱动 ES7210，双麦拾音 / 人声检测（屏幕无现象） | — |
| **07_ES8311** | I2S 驱动 ES8311，播放内置 PCM 音频（屏幕无现象） | — |

> 性能提示：`05_LVGL_Widgets` 在 Arduino 下约 50–60 fps；切到 **ESP-IDF 并启用双缓冲 + 双加速** 可获得更流畅的帧率。

---

## 9. 关键代码片段

### 9.1 显示屏初始化（QSPI + CO5300）

```cpp
#include "Arduino_GFX_Library.h"
#include "pin_config.h"
#include <Wire.h>

// 1) 创建 QSPI 数据总线
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS   /* CS    */, LCD_SCLK /* SCK   */,
    LCD_SDIO0/* D0    */, LCD_SDIO1/* D1    */,
    LCD_SDIO2/* D2    */, LCD_SDIO3/* D3    */);

// 2) 创建 CO5300 显示对象（466×466，带偏移参数 6,0,0,0）
Arduino_GFX *gfx = new Arduino_CO5300(
    bus, LCD_RESET /* RST */, 0 /* rotation */,
    LCD_WIDTH, LCD_HEIGHT, 6, 0, 0, 0);

void setup() {
    Serial.begin(115200);
    Wire.begin(IIC_SDA, IIC_SCL);     // 触摸/IMU/PMU 共用 I2C
    if (!gfx->begin()) Serial.println("gfx->begin() failed!");
    gfx->fillScreen(RGB565_BLACK);
    gfx->setBrightness(128);          // 亮度 0–255
    gfx->setCursor(10, 10);
    gfx->setTextColor(RGB565_RED);
    gfx->println("Hello World!");
}
```

### 9.2 触摸（CST9217 / CST92xx，SensorLib）

```cpp
#include "TouchDrvCSTXXX.hpp"
TouchDrvCST92xx touch;
int16_t x[5], y[5];
// 初始化（I2C: IIC_SDA / IIC_SCL, INT=TP_INT, RST=TP_RST）
// 读点：uint8_t n = touch.getPoint(x, y, touch.getSupportTouchPoint());
```

### 9.3 IMU（QMI8658，SensorLib）

```cpp
#include "SensorQMI8658.hpp"
SensorQMI8658 qmi;
IMUdata acc, gyr;
// qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
// 配置量程/ODR → enableAccelerometer() / enableGyroscope()
// 读取：if (qmi.getDataReady()) qmi.getAccelerometer(acc.x, acc.y, acc.z);
```

### 9.4 电源管理（AXP2101，XPowersLib）

```cpp
#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"
XPowersPMU PMU;
// PMU.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
// 读取电池：PMU.getBattVoltage();  PMU.getBatteryPercent();
// 充电状态：PMU.isCharging();      PMU.getBatteryPercent();
```

### 9.5 圆屏 / AMOLED 坐标偶数对齐（重要 gotcha）

CO5300 刷新窗口要求坐标对齐，LVGL flush 前需把刷新区域对齐到偶数边界，否则会出现错位/花屏：

```cpp
void example_lvgl_rounder_cb(lv_disp_drv_t *disp_drv, lv_area_t *area) {
    if (area->x1 % 2 != 0) area->x1--;      // 起点向下取偶
    if (area->y1 % 2 != 0) area->y1--;
    if (area->x2 % 2 == 0) area->x2++;      // 终点向上取奇（保证宽高为偶）
    if (area->y2 % 2 == 0) area->y2++;
}
// lv_disp_drv_t.rounder_cb = example_lvgl_rounder_cb;
```

---

## 10. 技术方案与解决方案

### 10.1 显示与 GUI

- **驱动方案**：QSPI 四线高速接口 + `Arduino_CO5300`（Arduino）或 `esp_lcd` + QSPI panel（ESP-IDF）。
- **圆屏 UI**：466×466 为圆形显示区，UI 设计需考虑圆形可视区域，控件居中、避免四角放关键信息。
- **GUI 框架**：LVGL v8.4.0。可用 **SquareLine Studio** 可视化拖拽设计界面后导出到工程。
- **性能优化**：ESP-IDF 下启用 **双缓冲（double buffer）+ DMA / PPA 双加速**；务必开启 8 MB PSRAM 作为帧缓冲；刷新区域**偶数对齐**（见 [§9.5](#95-圆屏--amoled-坐标偶数对齐重要-gotcha)）。
- **亮度/省电**：`gfx->setBrightness(0–255)`；熄屏时渐变到 0 再关，结合 PWR 键做亮/熄屏（示例 03）。

### 10.2 音频与语音 AI（核心卖点）

- **硬件链路**：双麦克风 → **ES7210（ADC + AEC 回声消除）** → I2S → ESP32-S3；ESP32-S3 → I2S → **ES8311（DAC）** → 功放（PA=GPIO46）→ 扬声器。
- **回声消除（AEC）**：ES7210 + 双麦阵列，适合**近场/远场语音唤醒**与全双工对话（边播放边拾音不啸叫）。
- **应用方案**：直接刷 **小智 AI（XiaoZhi）** 固件即可获得完整语音对话能力，详见 [§11](#11-小智-ai-语音方案)。也可基于 ESP-SR（唤醒词 + 命令词）自建本地语音。
- **注意**：使用扬声器前务必拉高 PA（GPIO46）使能功放。

### 10.3 电源与电池

- **AXP2101** 统一管理：锂电池充电、库仑计 / 电量估算、多路电压输出。
- 通过 **XPowersLib** 读取电池电压、电流、电量百分比、充电状态，做低电量提示与省电策略。
- **电池接口**：MX1.25 2PIN，3.7 V 锂电池，板上充放电。脱机便携场景的供电核心。
- **PWR 键**：长按控制整机电源通断，可自定义功能（如亮/熄屏、电源开关）。

### 10.4 运动 / 姿态

- **QMI8658** 六轴：加速度 + 陀螺仪，可做计步、翻转检测、抬手亮屏、手势、屏幕方向自适应旋转。

### 10.5 触摸交互

- **CST9217** 电容触摸（I2C，INT=GPIO11）。SensorLib 的 `TouchDrvCST92xx` 提供多点坐标读取，配合 LVGL 输入设备驱动即可。

---

## 11. 小智 AI 语音方案

本板是 **小智 AI（XiaoZhi）** 官方适配机型，是最快落地语音对话 AI 的方案。

- **适配固件版本**：`v2.2.6`（ESP32-S3-Touch-AMOLED-1.75C）
- **固件 / 源码来源**：
  - 小智官方（含 Release 固件）：<https://github.com/78/xiaozhi-esp32/releases>
  - 微雪 AIChats 仓库：<https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32>
- **其他可选 AI 应用**：OpenClaw、ESP-Claw（微雪 AI 应用教程中均有指引）。
- **落地路径**：① 直接烧录官方 bin 快速体验；② 拉取源码用 ESP-IDF 自行编译、接入自己的后端 / 大模型。

> 该板的双麦 + ES7210 AEC + ES8311 + 扬声器组合，正是为这类语音对话 AI 量身设计。

---

## 12. 固件烧录与恢复

### 烧录测试固件

- 测试固件 bin 路径：`...\ESP32-S3-Touch-AMOLED-1.75C-Demo\Firmware`
- 工具：**Flash Download Tool**（`flash_download_tool_3.9.7.exe`）
- 步骤：
  1. 芯片选 **ESP32-S3**，接口选 **USB**。
  2. 选择对应 **COM 口**；**BAUD = 1152000**（最高）。
  3. 选择 bin 文件，下载地址填 **`0x00`**，并勾选最左侧复选框。
  4. 点击 **START** 开始烧录，完成后**复位**观察现象。

### 强制下载模式（烧录失败 / 卡在「等待上电同步中」）

> **完全断电 → 按住 BOOT → 重新上电** 进入强制下载模式即可烧录。烧录完成不会自动退出下载模式，需**再次断电重启**。

### 命令行替代（esptool）

```bash
# 读取实际 Flash 容量（用于确认 16MB / 32MB）
esptool.py --port <COM/ttyUSB> flash_id

# 全片擦除
esptool.py --chip esp32s3 --port <COM/ttyUSB> erase_flash

# 烧录到 0x0
esptool.py --chip esp32s3 --port <COM/ttyUSB> -b 1152000 write_flash 0x0 firmware.bin
```

---

## 13. 常见问题（FAQ）

| 问题 | 解决 |
|------|------|
| **烧录失败** | ①串口被占用 → 关闭串口监视器重试；②程序崩溃 → 完全断电，按住 BOOT 再上电进强制下载模式 |
| **卡在「等待上电同步中」** | 按住 BOOT 重新上电 |
| **查看 COM 口** | Win：`devmgmt.msc` / `mode`；Linux：`ls /dev/ttyUSB*`、`dmesg` |
| **需要更多库支持** | 到 [ESP32-display-support](https://github.com/) 提 issue（微雪工程师评估） |
| **界面设计** | 使用 **SquareLine Studio**（可视化 LVGL 设计） |
| **能否帮忙改代码** | 官方定位为开发板，不协助改代码；ESP32 生态成熟，建议自行 DIY |

---

## 14. 资料下载与链接

| 资料 | 链接 |
|------|------|
| 官方文档主页 | <https://docs.waveshare.net/ESP32-S3-Touch-AMOLED-1.75C/> |
| 示例程序仓库（GitHub） | <https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C> |
| **原理图 PDF** | <https://www.waveshare.net/w/upload/8/83/ESP32-S3-Touch-AMOLED-1.75C-schematic.pdf> |
| ESP32-S3 数据手册（英） | <https://documentation.espressif.com/esp32-s3_datasheet_en.pdf> |
| ESP32-S3 技术参考手册（英） | <https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf> |
| QMI8658 数据手册 | <https://www.waveshare.net/w/upload/5/5f/QMI8658C.pdf> |
| AXP2101 数据手册 | <https://www.waveshare.net/w/upload/e/ed/X-power-AXP2101_SWcharge_V1.0.pdf> |
| ES8311 数据手册 | <https://www.waveshare.net/w/upload/6/65/ES8311.DS.pdf> |
| ES8311 用户手册 | <https://www.waveshare.net/w/upload/5/56/ES8311.user.Guide.pdf> |
| 小智 AI 固件 / 源码 | <https://github.com/78/xiaozhi-esp32/releases> |
| 微雪 AIChats（小智移植） | <https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32> |

---

## 15. 注意事项与待核实项

整理过程中发现官方文档存在若干不一致或需实测确认之处，开发前请留意：

1. **Flash 容量不一致**：产品特性写「外接 **16 MB** Flash」，板载资源写「**32 MB** NOR Flash」。
   → 以 `esptool.py flash_id` **实测为准**，并据此设置 Arduino 的 Flash Size 与分区表。
2. **显示驱动 IC 命名**：官方规格为 **CO5300**；部分示例文字残留了 `SH8601`/`ST7789` 字样（疑似从兄弟板复制）。**实际驱动类是 `Arduino_CO5300`**，以代码为准（CO5300 与 SH8601 为兼容的 QSPI AMOLED 驱动）。
3. **触摸 IC 命名**：官方规格为 **CST9217**；部分示例文字提到 `FT3168`（疑似复制残留）。**实际示例用 `TouchDrvCST92xx`**，以 CST9217 / CST92xx 为准。
4. **RTC 是否为独立 PCF85063**：概述提到「RTC」，SensorLib 也支持 PCF85063，但板载资源清单、数据手册列表、`pin_config.h` 中均未单独列出。
   → 请**对照原理图确认**是否为独立 PCF85063（I2C 0x51），还是由 AXP2101 提供。
5. **I2O 扩展芯片（XCA9554/PCA9554）**：Arduino 文档 05 示例文字提到 `expander`/`Adafruit_XCA9554`，但 `pin_config.h` 与该板示例头部未见定义，疑为兄弟板残留。**以原理图为准**。
6. **I2C 设备地址**：[§5](#5-总线架构i2c--qspi--i2s) 中地址为该类芯片的常见默认值，**建议上电后做一次 I2C 扫描**确认。
7. **ESP-IDF 教程页未上线**：官方 ESP-IDF 专属页当前 404，IDF 工程可暂以小智 AI 项目为参考。

> 凡涉及"接错可能损坏硬件"或量产的判断，请**以官方原理图 PDF 为最终依据**。
