# 系统信息 UI

核对日期：2026-10-04。入口 `main/app_sys.c` / `app_sys`，沿用启动器的标题、返回按钮和电量环。
466×466 圆屏、黑白灰点阵语言、中英文切换；底部三枚按钮切换总览、内存、设备。

## 页面与数据来源

| 页面 | 显示内容 | 数据来源及含义 |
| --- | --- | --- |
| 总览 | 使用率点阵环、已用/可用、堆内存总量 | 内部 RAM 与 PSRAM 两个互斥的可分配堆区域相加；使用率按字节加权，四舍五入到整数 |
| 内存 | 各自已用/总量、进度条、可用、最大连续块、历史最低可用 | `heap_caps_get_info` / `heap_caps_get_total_size`；历史最低值由 ESP-IDF 自启动以来维护 |
| 设备 | 芯片目标、核心数、配置 CPU 频率、Flash/PSRAM 物理容量、固件/SDK 版本、运行时长、任务数 | `esp_chip_info`、构建配置、`esp_flash_get_size`、`esp_psram_get_size`、app descriptor、`esp_get_idf_version`、`esp_timer_get_time`、`uxTaskGetNumberOfTasks` |

内部 RAM 使用 `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`，PSRAM 使用
`MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`，避免把同一块内存重复相加。
“已用” = 堆容量 − 空闲量，包含分配器开销，不表示某个 App 独占内存，也不等同全部物理 RAM。
设备页的 PSRAM 是物理容量；内存页是已注册到可分配堆的容量，两者可能不同。
小于 1MiB 显示 KiB，其余显示 MiB。CPU 频率是配置值，不表示实时频率或 CPU 占用率。

接口依据：[ESP-IDF 6.0.1 堆统计](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/api-reference/system/heap_debug.html)、
[能力分配](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/api-reference/system/mem_alloc.html)。

## 刷新与生命周期

`launcher → sys_enter → select_page → sys_tick` 读取首次快照；
`sys_tick → memory_draw / details_draw / device_draw` 在 LVGL 线程绘制。
`app_sys.tick_ms=1000`，同时在采样入口限制 1Hz；内存页仅在快照变化时重绘，设备页随秒更新时长。
`visibility(false)` 暂停采样，恢复时立刻读取新快照；`sys_exit` 清空对象引用。
没有新增任务、计时器或后台采样。运行时长保留 64 位微秒来源，小时不截断，避免约 49 天溢出。

没有 PSRAM 时总览仍可显示内部 RAM，PSRAM 明细显示 `--`。
容量为零、空闲超过总量、最大连续块/历史最低值超过当前空闲均视为不可用，避免错误比例和无符号下溢。
Flash 查询失败显示 `--`；0%/100% 均可正常绘制。
固件版本最多读取 descriptor 的 31 个有效字节，布局覆盖带提交后缀的版本。

## 字体与容量防线

局部文本复用 `font_weather_16` 的紧凑 ASCII，使用私有 RAM 副本设置 `font_cn16` fallback，
不改变天气或其他页面的全局字体。不能用字符宽 16px 的 `unscii_16` 排紧凑英文按钮。
自绘文本设置 `text_local`，让 LVGL 安全复制栈上格式化结果，并绑定绘制对象以供原生任务审计。

`tools/gen_font_cn.swift` 保留历史字集，追加 System 字符串中的中文。
裁掉 4bpp 量化后完全透明的外边缘；保持所有非透明像素、advance、基线位置和字体行高。
奇数宽的字形按连续像素流打包，不给每行补齐字节。
新增 15 个字后 bitmap 从未裁剪的 46,208B 降到 36,756B。
`tests/system/font_semantics.json` 固定修改前 346 个字的基线相对像素和字距，
`tools/check_system_font.py` 逐字核对，不能重写基线掩盖显示变化。

## 验证与验收

`tests/host/CMakeLists.txt` 的 `system_memory_and_layout` 使用真实 `app_sys.c` 和 LVGL renderer，
仅替换 ESP-IDF 数据服务。22 次原生渲染覆盖中英文、所有标签字形、圆屏边界和重叠、
RAM/PSRAM 互斥统计、占用加权、0%/100%、缺失/错误数据、5000 小时、
1Hz/稳定时不重绘/遮挡恢复、三页点击及重复进入退出。
`system_font_pixels` 保护全部既有中文字的像素语义；其余主机回归保护天气、OTA、音频、水平仪和抛硬币等调用方。

本轮 LVGL 9.5 与发布用 9.6 各 14 组主机回归通过，ESP-IDF 6.0.1 本地固件构建通过。
beta.17 标签构建通过，发布镜像 3,139,552B，4MiB 槽剩余 1,054,752B；
设备已 USB 安装相同发布镜像并正常启动，ELF 前缀与发布包匹配。
用户已批准两个 OTA 槽扩到 4MiB，并明确本次跳过备份；迁移及保留区域校验见
[分区迁移](./PORTING_NOTES.md#2026-10-04-分区扩容与usb迁移)。
原生审阅图使用测试快照，不是用户设备内存读数；设备端系统页实际数值、触摸及设备 OTA 下载/重启仍待用户验收。

人工验收：打开“系统”，切换三页；已用+可用约等于总量（显示有单位舍入），
RAM/PSRAM 进度与各自比例一致；设备页时长递增。盖住页面再返回，应恢复更新；
切换中英文后按钮无缺字、不越过电量环，返回与电量环沿用原行为。
