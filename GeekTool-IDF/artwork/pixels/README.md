# 方块 App 原生审阅

`native/` 内的 466×466 PNG 来自真实 `app_pixels.c`、`pixel_field.c` 与 LVGL
RGB565 framebuffer，由 `tests/host/pixel_tests.c` 导出；不是浏览器效果稿的重绘。
`pixels-review.png` 仅将这些原生画面拼为中英文对照，不改变 App 像素。

画面覆盖初始像素场、真实指针拖动、IMU 两峰摇动、配色与重聚按钮、
无 IMU、运行时 IMU 读取失败。系统返回采用启动器既有 48px 按钮及
`font_location_24` / `UI_FONT_SYM` 字体夹具；没有加入电量遥测。
两种语言各自从首次启动的默认配色与图案开始，便于直接比较；同一语言场景
内仍验证退出再进入保留会话选择。夹具只初始化会话选项，不修改方块坐标。

## 生成

在仓库根目录执行：

```sh
cmake -S GeekTool-IDF/tests/host -B /tmp/soround-pixel-tests
cmake --build /tmp/soround-pixel-tests --target pixel_tests -j 4
ctest --test-dir /tmp/soround-pixel-tests -R '^pixels_interaction_and_lifecycle$' --output-on-failure
python3 GeekTool-IDF/tools/render_pixel_review.py --tests /tmp/soround-pixel-tests/pixel_tests
```

导出脚本使用已安装的 Pillow。PPM 原始帧保存在独立临时目录，脚本完成或失败时
自动删除。主机构建目录仅用于验证，可在审阅结束后删除，不留下运行进程。

测试通过真实指针和传感器样本改变画面，不修改方块坐标；检查静止后无刷新、
摇动必须有峰→有效回落→第二峰，以及冷却与稳定重装；持续高加速度、
损坏样本及运行时 I2C 失败不能拼成双峰。另检查遮挡停止 IMU 与运动计算、恢复不补算后台时长、
触摸降级可用、共享返回、CPU 锁释放、重复进入退出及分配失败清理。
同时核对每个真实标签的字形与 466×466 圆屏内边界。

核对日期：2026-10-09。主机验证与审阅导出不等于真机触摸、摇动、AMOLED
显示、功耗或用户验收；这些仍需设备验证。

本地 LVGL 9.5.0 / Debug 的像素交互与生命周期测试已通过，含中英文缺字、
圆屏边界及 `font_location_24` 历史“效”“生”和新增“块”字保护。
同时通过 12 组直接相关回归（像素、6 个启动场景、设置/水平仪、系统界面、
历史字形、实体按键、流体/固定迷宫）。已导出 14 张原生状态图及 1 张审阅联图。
