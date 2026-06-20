# GeekTool —— 圆屏多功能仪表平台

ESP32-S3-Touch-AMOLED-1.75C 上的「极客多功能仪表」:**径向启动器 + 可插拔 App 框架**。
底层(CO5300 显示 / CST9217 触摸 / LVGL)沿用已验证写法;每个工具是一个独立 app,
共用同一套圆屏列表组件和系统电量环。

已内置 3 个 app:**WiFi**(扫描 + 连接)、**I2C**(扫描板载芯片)、**System**(芯片/内存/运行时长)。

---

## 文件结构

| 文件 | 作用 |
|------|------|
| `GeekTool.ino` | 引擎:HAL 初始化、径向启动器、App 开关与生命周期调度、顶部电量环/返回 |
| `app.h` | App 接口 `app_t`、主题色、注册表与 HAL 全局的声明 |
| `ui_list.h/.cpp` | 圆屏居中聚焦滚动列表(WiFi/I2C/System 共用的可复用组件) |
| `app_wifi.cpp` | WiFi app:扫描 + 点击连接(密码键盘) |
| `app_i2c.cpp` | I2C 扫描 app:列出地址 + 已知芯片名 |
| `app_sys.cpp` | 系统信息 app:芯片/核心/内存/运行时长(实时刷新) |
| `pin_config.h` | 官方引脚定义 |

---

## 导航

- 启动器:**横向轮播** —— 一次只居中显示**一个大图标**;**左右滑**或点**左右箭头**切换 app,点中间大图标进入。
- 工具内返回启动器:**向右滑**,或点**左上角 ‹** 按钮(两种都行)。
- 流畅度三个旋钮:① `lv_conf.h` 把 `LV_DISP_DEF_REFR_PERIOD` 从 30 改 **16**(帧率上限 33→60);② 绘制缓冲已用 1/4 屏,若仍有分块撕裂,按 `GeekTool.ino` 里的注释改成"全屏单缓冲(PSRAM)";③ 图标大小见顶部 `ICON`。
- WiFi 连接:点某个网络 → 输密码 → 点绿色 **Connect**(详见上一个工程的说明)。

---

## 编译环境(同 WiFiList 工程)

- 库:`lvgl 8.4.0` / `GFX Library for Arduino 1.6.4` / `SensorLib 0.3.3`
- **esp32 core 用 3.3.5**(3.3.10 与 GFX 1.6.4 不兼容)
- 板卡:ESP32S3 Dev Module,**PSRAM: OPI PSRAM**,USB CDC On Boot: Enabled,Flash 16MB
- `lv_conf.h` 关键项:`LV_COLOR_16_SWAP=1`、`LV_FONT_MONTSERRAT_14=1`、`LV_FONT_MONTSERRAT_20=1`、
  `LV_USE_KEYBOARD=1`、`LV_USE_TEXTAREA=1`

---

## 怎么加一个新工具(3 步)

以「加一个传感器仪表」为例:

**1. 写 `app_xxx.cpp`** —— 实现三个函数 + 定义 `app_t`:

```cpp
#include "app.h"
#include "ui_list.h"

static void xxx_enter(lv_obj_t *parent) {
    lv_obj_t *list = ui_list_create(parent);
    ui_list_row(list, "Hello", "world", COL_OK);
    ui_list_relayout(list);
}
static void xxx_tick(void) { /* 周期刷新,可为 NULL */ }
static void xxx_exit(void) { /* 清理,可为 NULL */ }

const app_t app_xxx = { "Sensor", COL_SYS, xxx_enter, xxx_tick, xxx_exit };
```

**2. 在 `app.h` 声明**:`extern const app_t app_xxx;`

**3. 在 `GeekTool.ino` 注册一行**:把 `&app_xxx` 加进 `APPS[]`。

启动器会自动多出一个圆点,曲率列表、电量环、返回手势全都白送。

---

## 备注 / 下一步

- **电量环现在是占位 72%**:接 AXP2101(XPowersLib)读真实电量后,把 `GeekTool.ino`
  里 `lv_arc_set_value(batt, 72)` 换成真实值即可。
- 中文文本需中文字体子集(默认字体只有 ASCII);WiFi 名里的中文会显示成方块。
- 之后可加:同心环电量表、水平仪(QMI8658)、实时频谱(双麦 FFT)、REST 轮询卡等。
