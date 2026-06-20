# WiFi 列表压测 — ESP32-S3-Touch-AMOLED-1.75C

圆屏可滚动 WiFi 列表,用来验证「复杂滚动界面在本板上扛不扛得住」。
界面特点:**纯黑底 + 顶部系统电量环 + 居中聚焦曲率 + 边缘虚化 + 实时 FPS**。

底层初始化(CO5300 显示 / CST9217 触摸 / LVGL 移植)沿用微雪官方 `05_LVGL_Widgets` 例程的已验证写法,只替换了 UI 层,所以点亮风险很低。

---

## 1. 需要的库(Arduino 库管理器或离线安装,版本要对)

| 库 | 版本 | 提供 |
|----|------|------|
| `lvgl` | **8.4.0** | GUI 框架 |
| `GFX Library for Arduino` | **1.6.4** | `Arduino_CO5300`、`Arduino_ESP32QSPI` |
| `SensorLib` | **0.3.3** | `TouchDrvCST92xx`(触摸) |

> 这三个库在微雪官方示例包的 `Arduino/libraries` 里都有,直接拷到
> `~/Documents/Arduino/libraries/` 即可。版本不要混用(尤其 LVGL v8 ≠ v9)。

`pin_config.h` 已放在本工程目录里(自带),无需依赖 Mylibrary。

---

## 2. lv_conf.h 配置(关键)

LVGL 需要一个 `lv_conf.h`。最省事的做法:用官方示例包里那份(已为本板调好),
然后确认/修改下面几项:

```c
#define LV_COLOR_DEPTH        16
#define LV_COLOR_16_SWAP      1        // 必须为 1,与本工程 flush 的大端分支一致
#define LV_FONT_MONTSERRAT_14 1        // 标题 / FPS
#define LV_FONT_MONTSERRAT_20 1        // ← 记得打开!SSID 用
#define LV_MEM_SIZE   (48U * 1024U)    // 不够可调大;也可改用 PSRAM
#define LV_TICK_CUSTOM        0        // 本工程用 esp_timer 调 lv_tick_inc
#define LV_USE_KEYBOARD       1        // 连接 WiFi 的密码键盘(v8 默认就是 1)
#define LV_USE_TEXTAREA       1        // 密码输入框(默认 1)
```

## 如何连接 WiFi

**点一下**列表里的某个 WiFi(短按,不是滑动):
- 开放网络 → 直接连接;
- 加密网络 → 弹出输入框 + 键盘,打字输入密码(明文显示,看得清),
  然后点屏幕中部的绿色 **Connect** 按钮连接,**Cancel** 取消。

> 为什么不用键盘自带的 ✓:LVGL 键盘的 ✓ 在右下角,正好被圆形屏幕裁掉了,
> 所以改用屏幕中部(圆最宽处)的大号 Connect 按钮提交。键盘只用来打字。

连接状态显示在顶部标题:`Connecting...` → 成功变绿 `OK: <ssid>`(串口会打印 IP),失败变红 `Connect failed`(15 秒超时)。

> 圆屏键盘最底排边角仍会被裁一点,但字母/数字都在上面几排,够用;
> 之后可换成圆屏友好的输入方式(BLE 配网 / 手机端输入)。

> 如果编译报 `lv_font_montserrat_20` 未定义,就是上面那行没开。

---

## 3. Arduino IDE 板卡设置

- 开发板:**ESP32S3 Dev Module**
- **PSRAM: OPI PSRAM**(8MB 叠封 PSRAM,必须开)
- USB CDC On Boot: **Enabled**(这样 `Serial` 走 USB,能看日志)
- Flash Size: **16MB**(或实测的 32MB)
- Partition Scheme:默认即可(APP 分区够用)
- CPU Frequency: 240MHz
- Upload Speed: 921600

接上 Type-C,选对应串口,编译上传。若卡在「等待上电同步」,按住 BOOT 重新上电进下载模式。

---

## 4. 上板后应该看到什么(验证点)

1. 黑底圆屏,顶部一圈绿色电量环(占位 72%),顶部「Wi-Fi」标题,底部蓝色「FPS」。
2. 开机先显示一组 Demo 行;约 1–2 秒后 WiFi 扫描完成,自动替换成**你周围真实的 WiFi**(标题数字 = 扫到的数量),不足 14 行会用 Demo 补足以便滚动。
3. **手指上下滑动列表** —— 中间行最大最亮,上下行随圆边缩小变暗;松手吸附到中心。
4. **看底部 FPS**:滑动时显示真实渲染帧率(参考 `05_LVGL_Widgets` 官方实测 50–60fps)。
   - 静止时 FPS 接近 0 是**正常的**:LVGL 只在画面变化时才重绘,不动=不刷=省电。

把这一关跑通,就证明了「设计 → 代码 → 上板 → 性能」整条链路成立。

---

## 5. 已知取舍 / 可能要调的地方

- **中文 SSID**:默认字体不含中文,周围若有中文名 WiFi 会显示成方块。需要的话再加一个中文字体子集(几百 KB),这是字库取舍,不影响能否运行。
- **触摸方向**:若发现滑动方向反了,改 `setMirrorXY(true, true)` 的两个参数即可。
- **电量环是占位值 72%**:接 `XPowersLib` 读 AXP2101 真实电量后,把 `lv_arc_set_value(batt, 72)` 换成真实百分比即可(下一步做)。
- **绘制缓冲**:用的是 `屏幕/10` 双缓冲(放内部 DMA RAM,约 87KB);若与 WiFi 抢内存导致分配失败,代码会自动回退到 PSRAM。

---

## 6. 已踩坑记录

### 6.1 GFX 1.6.4 与 Arduino-ESP32 core 3.3.10 编译不兼容

- 问题描述:按 README 安装 `GFX Library for Arduino 1.6.4` 后,若本机仍是 Arduino-ESP32 core `3.3.10`,编译会在 `Arduino_ESP32SPI.cpp` / `Arduino_ESP32SPIDMA.cpp` 报 `spiFrequencyToClockDiv` 参数不匹配。
- 出现原因:core `3.3.10` 中 `spiFrequencyToClockDiv` 函数签名已变化,而 GFX `1.6.4` 仍按旧签名调用。
- 影响范围:使用 `GFX Library for Arduino 1.6.4` 的 ESP32-S3 编译环境;即使当前草图主要用 QSPI,Arduino 仍会编译库内相关源码。
- 解决方案:按微雪示例版本矩阵,将 `esp32:esp32` core 对齐到 `3.3.5`,再编译上传。
- 后续注意:不要只看三个 Arduino 库版本,还要同步确认 `arduino-cli core list` 中 `esp32:esp32` 是兼容版本。

### 6.2 lvgl 目录混装导致缺少 `lv_draw_rect.h`

- 问题描述:重新安装 `lvgl 8.4.0` 后,编译可能报 `fatal error: lv_draw_rect.h: No such file or directory`。
- 出现原因:从旧版本替换到 `8.4.0` 时,本机 `~/Documents/Arduino/libraries/lvgl` 目录出现残留/混装,部分文件被放进 `src/draw 2/` 这类重复目录。
- 影响范围:本机 Arduino 全局 `lvgl` 库目录;会影响所有依赖 LVGL v8 头文件结构的草图。
- 解决方案:先 `arduino-cli lib uninstall lvgl`,再 `arduino-cli lib install "lvgl@8.4.0"` 干净重装。
- 后续注意:版本号显示正确不代表目录一定干净,遇到缺头文件时先检查是否存在 `* 2` 这类重复目录。

---

## 7. 下一步

这一屏验证 OK 后,就按这套(已验证的底层 + LVGL UI)继续做:同心环电量仪表、
径向启动器、I2C 扫描器等,逐个加成「极客多功能仪表」的工具卡。
