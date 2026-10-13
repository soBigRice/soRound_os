# 粒子流体与固定迷宫关卡

核对日期：2026-10-13。用户反馈染料卡顿并明确要求移除；当前只保留原粒子流体。
染料入口、模式切换和配色/暂停/重置控件及CPU染料求解器均已移除，不保留隐藏后端。
此次修改的主机验证与真机状态见文末；迷宫、表盘与已验收天气不在本次修改范围。
表盘索引和常显逻辑见[锁屏表盘](./WATCHFACES_UI.md)，历史设计见[原型记录](./artwork/analog-play/README.md)。

## 流体：直接进入粒子

`launcher.c:enter_app → app_fluid.enter → fluid_enter → particle_enter`。
直接显示240个白/红粒子；连续Verlet积分、空间哈希碰撞、464×464 RGB565画布和8条脏带保持。
原IMU重力、无IMU向下重力、静止休眠、右滑返回和标题/BOOT返回保持；不新增NVS键。
`fluid_frame`按经过时间执行固定8.25ms物理步，20ms请求绘制；请求周期不等于实测50fps。

| 生命周期 | 行为 |
| --- | --- |
| 活动 | 原粒子计算/增量绘制，持有CPU频率锁 |
| 静止 | 保留33ms IMU检查，停止模拟和推屏、释放频率锁；慢倾斜累计变化可唤醒 |
| 遮挡 | 暂停定时器、释放锁；恢复时重置时钟和物理余数，不补算后台时长 |
| 退出 | `stop_simulation`先停止定时器/删除锁、清全局引用；父屏实际销毁时画布DELETE释放所属内存，兼容异步删屏 |
| 分配失败 | 两块分配均清理，显示“内存不足”，保留系统返回；退出后重新进入可重试，不持有活动频率锁 |

画布430592B位于PSRAM，仅应用存续时保留。粒子/哈希同块分配方式和物理常量未改。
此前染料采用独立CPU求解与466×466输出；用户实际设备反馈卡顿，2026-10-13选择移除。
这条经验只适用于当前CPU染料路径，不能据此断言所有染料模拟不可实现，也不能擅自削减其他已确认功能。
历史双模式截图及浏览器原型保留为设计记录，不代表当前固件。

## 迷宫：三章十二关

固定4×4、5×5、6×6各四关，最短路径依次6、8、…、28步，允许自由选关与重玩。
唯一地图来源为`artwork/analog-play/levels.js:authored`；`tools/gen_maze_levels.cjs`导出
只读`main/maze_levels.c`，`maze_levels.h`定义墙位序N/E/S/W及网格大小/名称/最短步数。
进入/重玩不再随机生成；没有倒计时、生命数、陷阱或解锁限制。

| 状态 | 触发与转换 |
| --- | --- |
| MENU | 进入应用显示12个关卡按钮；选择`maze_choose`→PLAY |
| PLAY | `maze_build`按实际格宽创建墙和目标；`maze_tick`读取IMU→原连续滚球积分/碰撞→目标距离小于12px→WIN |
| WIN | 保持本关完成画面，等待下一关或重玩；第12关下一步返回关卡菜单 |
| 返回 | PLAY/WIN先回MENU；MENU返回由启动器退出应用 |

状态重建用`maze_queue → lv_async_call(maze_rebuild)`，避免销毁当前点击事件的控件。
退出取消待执行回调并清空引用；遮挡恢复重置时间，避免补算后台时长。
完成位集和最近关卡只在本次开机内保留，不写NVS。无IMU显示明确提示，恢复后继续本关。
原重力常数、阻尼、限速和圆/墙连续碰撞模型保持；网格按276px棋盘计算，5×5边界逐点取整。
原生内容帧见[选关/游戏/完成](./artwork/analog-play/native/maze.png)。

## 防线与验证

`tests/host/play_tests.c`使用真实LVGL和粒子/迷宫控制器，仅替代硬件时钟、IMU、分配失败与PM接口。
粒子覆盖中英文直接进入、仅画布/提示对象、原模式/配色位置点击不切换、无IMU、遮挡停算与恢复时钟、两块分配失败及退出延迟释放。
`performance_tests.c`继续覆盖慢倾斜唤醒、频率锁、脏带覆盖所有修改像素及等时长物理步，不削弱原粒子防线。
12关检查墙对称/外墙封闭/全图连通及设计步数；通过IMU夹具驱动真实连续碰撞，全部进入WIN。
过关等待、不自动跳图、重玩/返回、无传感器及排队退出均有验证；不直接改球坐标制造通关。

新增中文包含全角标点。旧字体生成只扫描表意字，曾导致逗号/句号缺字；现扫描这些页面真实字符串并保留历史字集。
`gen_font_cn.swift`按需加入表意字/CJK标点/全角字符，`play_tests`逐标签检查缺字，
`check_system_font.py`保护已交付字形像素、字距和基线，不通过重写基准隐藏回归。

可重复运行（执行完删除自己的临时构建目录）：

```sh
node GeekTool-IDF/tools/gen_maze_levels.cjs
cmake -S GeekTool-IDF/tests/host -B /tmp/soround-play-tests
cmake --build /tmp/soround-play-tests --target play_tests performance_tests watchface_tests watchface_image_tests settings_level_tests system_ui_tests buttons_tests -j 6
ctest --test-dir /tmp/soround-play-tests -R 'fluid_particles|performance_regressions|watchface_|settings_controls|system_memory|system_font|physical_button' --output-on-failure
python3 GeekTool-IDF/tools/render_analog_play_review.py --play /tmp/soround-play-tests/play_tests --settings /tmp/soround-play-tests/settings_level_tests
idf.py -C GeekTool-IDF build
```

2026-10-13移除染料：`performance_regressions`与`fluid_particles_and_fixed_maze_levels`两组回归通过，
包含粒子生命周期、12关真实碰撞和原生指针表盘断言。原生466×466粒子画面已查看，无模式/染料控件。
首轮主机编译暴露表盘测试通过染料文件间接使用PI常量；改为相同本地数值，保持原断言，重建通过。
ESP-IDF6.0.1/LVGL9.5.0固件构建通过，镜像4154416B，SHA256
`670705a4722e427b71773b3a47b983e757ca5d65e1b3a3ee5f67e43267c7902f`。
粒子模拟/绘制/分配段与上一提交逐字比较（仅去掉染料分支）一致；链接映射无染料后端。
设备已装入`v1.7-beta.33-particles`，新ota_1 seq32从NEW由应用确认VALID，
原ota_0 seq31 VALID保留。物理应用/分区表/FAT摘要通过，boot/PHY/原ota_0保持，审计期间NVS未改写。
USB本轮未收到启动日志；以上首启结论来自真实OTA状态和物理应用核验，不能冒称收到日志。
复位恢复正常应用运行后关闭USB，临时源码/构建/原生测试图片已清理，完整私有Flash恢复备份及候选符号保留。
2026-10-13用户确认仅粒子、倾斜跟随和返回“都正常”；本轮真机已验收，纳入本地提交。
此前2026-10-08双模式版本的构建/发布结果仅属历史，见[beta.32记录](./PORTING_NOTES.md#2026-10-08-v17-beta32-指针表盘流体双模式与固定迷宫关卡)。
本次不改变依赖、8MiB OTA槽或文件区，不发布公网固件。
