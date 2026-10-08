# 流体双模式与固定迷宫关卡

核对日期：2026-10-08。用户确认保留原粒子模式，并增加可拖动搅开的柔和彩色染料。
当前工作区实现完成、本地自动验证通过；真机触摸、IMU手感、帧耗时、内存及功耗待验证。
表盘索引和常显逻辑见[锁屏表盘](./WATCHFACES_UI.md)，设计来源见[原型记录](./artwork/analog-play/README.md)。

## 流体：同一应用内切换

`launcher.c:enter_app → app_fluid.enter → fluid_enter → open_mode`。
顶部“粒子 / 染料”切换；每次只保留一个模拟后端。选择和配色在本次开机内保留，不新增NVS键。

| 模式 | 数据与绘制 | 输入及控制 |
| --- | --- | --- |
| 粒子 | 原240粒子、连续Verlet积分/空间哈希碰撞、464×464 RGB565画布与8条脏带 | 原IMU重力、静止休眠及右滑返回 |
| 染料 | `fluid_ink.c`64×64速度/压力/RGB密度网格，插值输出原生466×466 RGB565并比较8条脏带 | 触摸注入与搅动、倾斜变化搅动、三套配色、暂停/播放与重新注入 |

染料触摸事件`ink_touch`从真实`lv_indev`获取坐标；每7px补齐笔刷轨迹，注入颜色和局部速度。
每次新笔划切换颜色。配色为青蓝/陶橙、松绿/米金、烟紫/珊瑚；换色清空场并建立初始色带。
暂停停止输运，仍可绘入、换色和重新注入。染料画布不把笔刷右拖冒泡为返回，系统标题返回/BOOT仍可退出。
无IMU时染料触摸仍可工作；原粒子无IMU行为保持。

`fluid_frame → ink_frame → fluid_ink_step → ink_redraw → fluid_ink_render`。
速度和染料采用半拉格朗日平流，附加涡量、12次压力投影与圆形边界；dt上限25ms，注入速度限幅。
颜色按密度归一和覆盖率映射，避免混合次数增加后统一变白。内部网格与最终输出尺寸分别管理。
这是视觉模型，未作为定量流体仿真；没有移植浏览器WebGL，也没有引入GPU或第三方运行库。
选择依据为[作者的Stable Fluids论文](https://www.dgp.toronto.edu/public_user/stam/reality/Research/pdf/ns.pdf)，
2026-10-08核查；CPU定额求解适合现有ESP32/LVGL路径，浏览器GPU方案不适用此设备。
两种后端共用20ms定时请求；这不是实测50fps，实际帧耗时须在设备上测量。

| 生命周期 | 行为 |
| --- | --- |
| 活动 | 计算/绘制，持有CPU频率锁；染料只在倾斜变化时注入，固定姿态可自然静止 |
| 静止 | 保留33ms IMU唤醒检查，停止模拟和推屏、释放频率锁；慢倾斜累计变化也能唤醒 |
| 遮挡/暂停 | 暂停定时器、清触摸状态并释放锁；恢复时重置时钟，暂停态不被遮挡恢复自动播放 |
| 切换 | `stop_simulation`先停定时器/删除锁，再删旧`g_scene`；画布DELETE释放所属缓冲 |
| 退出 | 停止后端、清全局引用；父屏销毁释放剩余画布/模拟内存 |
| 分配失败 | 两块分配均清理，显示“内存不足”，仍可切换另一模式；不持有活动频率锁 |

染料场约234KiB、输出约424KiB，都用PSRAM；切换时释放旧后端后再分配，不长期叠加。
原生内容帧见[流体双模式](./artwork/analog-play/native/fluid.png)。共享启动器标题/电量环未叠加到此内容区截图。

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

`tests/host/play_tests.c`使用真实LVGL、两种流体、染料求解器和迷宫控制器，仅替代硬件时钟、IMU、分配失败与PM接口。
原生指针输入覆盖中英文模式切换、拖动、全部配色、暂停绘入、遮挡恢复、静止/慢倾斜唤醒和退出释放。
求解器检查非有限/越界输入、过大搅动力/时间步、数值有限性、输出越界哨兵和重复渲染无脏区。
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
ctest --test-dir /tmp/soround-play-tests -R 'fluid_modes|performance_regressions|watchface_|settings_controls|system_memory|system_font|physical_button' --output-on-failure
python3 GeekTool-IDF/tools/render_analog_play_review.py --play /tmp/soround-play-tests/play_tests --settings /tmp/soround-play-tests/settings_level_tests
idf.py -C GeekTool-IDF build
```

本轮8组相关回归通过；原生466×466图已实际查看。审阅脚本自动清理临时PPM，只保留PNG。
本地ESP-IDF6.0.1/LVGL9.5、ESP32-S3构建通过，工作区基准`2963a99`，版本`v1.7-2-g2963a99-dirty`。
应用镜像4,152,592B，4MiB槽余41,712B；未改分区、发布流程或依赖。
SHA256：`79083d390583f27284cb3b8c29ea1145934cfd237c07290ae9f932cf5b69bfbb`。
以上为开发阶段构建；用户已要求内测发布，正式依赖与发布镜像证据见[发布记录](./PORTING_NOTES.md#2026-10-08-v17-beta32-指针表盘流体双模式与固定迷宫关卡)。
未烧录，主机耗时不等于真机性能，也不代表用户观感验收。

真机最小验收：设置选遍六款HAND→活动/AOD/唤醒；流体两模式反复切换→三配色/拖搅/暂停/遮挡/退出→检查稳定内存、帧耗时与静止唤醒；
迷宫各网格选一关→倾斜转弯/碰撞/重玩/过关后下一关/返回。
