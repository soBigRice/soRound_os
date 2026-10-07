# 4秒启动与自检状态审阅

2026-10-07，由当前 `identity_ui.c`、实际LVGL9.5与 `identity_ui_tests` 导出466×466原生画面。
网络与错误状态为测试输入，画面不是设备截图；自动检查字形和圆形边界后，已查看中英文状态图。

- [boot.mp4](./boot.mp4)：120帧 / 30fps / 4秒单次最短动画。实际固件在检查未完成时继续保持检查页，视频不模拟硬件初始化或OTA。
- `boot-check-{zh,en}.png`：等待系统检查完成。
- `boot-offline-{zh,en}.png`：IP未就绪，提示可离线使用。
- `boot-network-failed-{zh,en}.png`：HTTPS联网检查失败，提示可离线使用。
- `boot-failed-{zh,en}.png`：核心/OTA自检错误，保持覆盖。

时间轴与资源入口见 [标志设计](../../README.md)，放行、回滚、探测与验收边界见 [启动逻辑](../../../../STARTUP.md)。
复现：给 `identity_ui_tests` 传入已存在的临时目录，导出PPM；选定状态转PNG，`boot-frame-%03d.ppm`按30fps转H.264。
临时构建、原始PPM和日志验证后删除，只保留这些可审阅资产。
