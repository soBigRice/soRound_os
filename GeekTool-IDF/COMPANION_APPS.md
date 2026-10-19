# 生态箱、专注、声音胶囊与遥控台

核对日期：2026-10-19。用户已选遥控台第3版，并批准三个新App、手动Mac本地桥接、独立录音分区；**设备备份、烧录和分区迁移仍需另行确认**。本入口记录本地实现，不代表真机验收或已经发布。

## 模块地图

| 入口 | 调用链和边界 | 详细说明 |
| --- | --- | --- |
| `launcher.c::APPS[20..22]` | Garden / Focus / Capsules 追加到原20项后，原索引不变；新图标由`companion_icons.c`适配现有图标集 | [生态箱与专注](COMPANION_GARDEN_FOCUS.md) |
| `app_garden.c` | 触摸→浇水动画→`garden_state`共享工作副本→NVS成功commit后清dirty；专注完成去重后增成长 | [状态、素材与回归](COMPANION_GARDEN_FOCUS.md) |
| `app_focus.c` | 单调时钟驱动25分钟计时，暂停/恢复/重置；隐藏继续计时，停止视觉；退出结束会话；音景与告警走原输出worker | [音频与录音](CAPSULES.md) |
| `app_capsules.c` | UI请求→`capsule_recorder`任务→原麦克风/音频总线→只写临时WAV→完整提交→文件列表/回放/两步删除 | [存储保护、离线迁移](CAPSULES.md) |
| `app_mouse.c` | 原鼠标相对位移/轻点/左右键/滚轮/拖拽→原HID；三模式同连接，隐藏/切换/断连释放输入 | 本页下节 |
| `mac-companion` | 系统Now Playing→固定BSD适配器→CoreText/ImageIO→加密BLE→`remote_media`完整校验→UI自有PSRAM图层 | [Mac运行、协议与限制](../mac-companion/README.md) |

## 遥控台第3版

鼠标页使用留白触控区、独立滚轮、圆形左右键和明确的拖拽锁定。原`GAIN/TAP_MS/TAP_MOVE/SEND_MS`、鼠标四字节Report及键盘/媒体Report保持原约定；不改变坐标方向或用全屏滑动退出吞掉鼠标输入。演示计时、翻页和离线状态保留。

音乐主页面只有上一曲、播放/暂停、下一曲三个主按钮，仍控制系统当前媒体目标。**长按底部“音乐”**打开原有音量增减/静音，再次长按或返回可收起；不删除这三项已有控制。

`remote_media_get_snapshot()`只提供真实主机回传状态。`app_mouse::update_media_view()`在revision变化时把完整466×466 RGB565封面和350×180 A8文字遮罩复制到UI专属PSRAM；封面慢转、文字固定，暂停/遮挡不补算动画。图层内存不足或复制版本已失效时不发布错误图像。退出先删除LVGL对象/缓存，再释放UI内存。

没有桥接时三键仍可用，显示“等待电脑同步曲目信息”和播放/暂停复合图标，不猜测真实暂停状态。无封面、换曲、断连或桥接心跳失效清除旧图；无匹配本地歌词时显示曲目信息。歌词不会自动联网查找：Mac工具支持显式绑定曲名的本地LRC。

原生截图`artwork/remote/native/`来自真实App与LVGL；HID连接、封面和歌词是明确的测试输入。`remote-media-lyrics-fixture.png`使用合成渐变及CoreText测试文字，**不是电脑正在播放的歌曲，也不是真机截图**。Lucide遥控页30px图标的原SVG/ISC许可位于`artwork/remote/icons/`，转换入口`tools/gen_remote_icons.py`。

## 验证和发行边界

- 主机测试：`tests/host`，含原输入释放/OTA/启动/天气/木鱼、录音五项与新旧图标适配；注册校验明确为23项并保留`APPS[19]==app_pixels`。
- 生态箱/专注：`tests/companion_apps`；真实状态流、NVS错误和原生双语渲染。
- 媒体解析与GATT：`tests/remote_media/run.sh`；真实解析器的ASan/UBSan边界验证。
- Mac：`mac-companion/build.sh`和产物`--self-test`；实际只读Now Playing探测当前返回`null`，不能据此确认各播放器兼容或实际封面吞吐。
- 离线录音分区：`tests/capsule_prepare_tests.py`；真实SDK生成镜像，但没有访问设备。

2026-10-19最终本地复核：主机47项、生态箱/专注2项、离线录音镜像3项全部通过；媒体ASan/UBSan与Mac自测通过。ESP-IDF 6.0.1完整固件构建成功，镜像`0x4cd3d0`字节，8MiB代码槽余40%。[原生界面合览](artwork/companion-apps/native/companion-overview.png)中的歌曲与录音状态为测试输入。

候选使用双8MiB代码槽；旧4MiB槽无法安装约5MB镜像。新录音布局保持只读表盘资源与独立可写卷，旧FAT必须在完整备份真实文件后重建，**不能截断或直接对现有raw FAT尝试RW挂载**。尚未迁移的设备不具备录音保存条件，明确报“存储未配置”。

工作区另有A套启动器图标任务，同一版本的提交、标签、CI与发布由该任务统一处理；本任务不自行提交/推送/发布。最终发行需重新构建准确提交并验证23入口。本地编译、主机渲染与协议测试均不替代真实BLE、触摸、音频、掉电恢复、帧耗时或用户审美验收。
