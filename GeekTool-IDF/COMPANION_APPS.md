# 生态箱、专注、声音胶囊与遥控台

核对日期：2026-10-20。用户已选遥控台第3版，并批准三个新App、手动Mac本地桥接、独立录音分区；**设备备份、烧录和分区迁移仍需另行确认**。启动器缺字修复和居中三键已发布为beta.35；本轮没有更新设备。

## 模块地图

| 入口 | 调用链和边界 | 详细说明 |
| --- | --- | --- |
| `launcher.c::APPS[20..22]` | Garden / Focus / Capsules 追加到原20项后，原索引不变；新图标由`companion_icons.c`适配现有图标集 | [生态箱与专注](COMPANION_GARDEN_FOCUS.md) |
| `app_garden.c` | 触摸→浇水动画→`garden_state`共享工作副本→NVS成功commit后清dirty；专注完成去重后增成长 | [状态、素材与回归](COMPANION_GARDEN_FOCUS.md) |
| `app_focus.c` | 单调时钟驱动25分钟计时，暂停/恢复/重置；隐藏继续计时，停止视觉；退出结束会话；音景与告警走原输出worker | [音频与录音](CAPSULES.md) |
| `app_capsules.c` | UI请求→`capsule_recorder`任务→原麦克风/音频总线→只写临时WAV→完整提交→文件列表/回放/两步删除 | [存储保护、离线迁移](CAPSULES.md) |
| `app_mouse.c` | 原鼠标相对位移/轻点/左右键/滚轮/拖拽→原HID；三模式同连接，隐藏/切换/断连释放输入 | 本页下节 |
| `mac-companion` | 系统Now Playing→固定BSD适配器→CoreText/ImageIO→加密BLE→`remote_media`完整校验→UI自有PSRAM图层 | [Mac运行、协议与限制](../mac-companion/README.md) |

## 启动器名称字库

2026-10-19 用户报告新版启动器名称出现方块字。实际链路是 `launcher.c::apply_app()/swap_exec()` → `set_app_name()` → `g_name`；三个新App现在统一将名称绑定到 `font_companion_24`，旧App继续使用原 `font_location_24`。进入App的共享标题此前已经绑定新字库，正文与生成器无需改动。

已证实根因：旧 `font_location_24` 精简表缺“态、箱、专、注”，新 `font_companion_24` 全部覆盖，但真实启动器曾始终固定旧字体。`tools/preview_launcher_icons.c` 此前已为新App选择正确字体，导致只看预览没有覆盖实际缺陷。以后新增App不能只验证预览；名称更新和字体绑定必须覆盖真实启动器直接选中、动画中点、进入/返回和回绕路径。

回归 `tests/host::launcher_bilingual_app_name_glyphs` 从真实 `app_main()` 启动，经真实箭头事件/动画与指针入页，验证两种语言、23项名称字形、三新App标题和返回后名称字体。fixture 的三个新App名称保留生产大小写，确保真正命中中文映射。新增用例在修复前复现 `launcher missing U+6001: 生态箱`，修复后与六项既有启动流程均通过；实际启动器截图保留在 `artwork/companion-apps/native/launcher-font-fix/`。

核对的本地运行版本为 LVGL 9.5.0；官方 [v9.6.0 字库定义](https://github.com/lvgl/lvgl/blob/v9.6.0/include/lvgl/font/lv_font_fmt_txt.h)与[查表实现](https://github.com/lvgl/lvgl/blob/v9.6.0/src/font/fmt_txt/lv_font_fmt_txt.c)仍支持当前静态4bpp/cmap数据，缺字同样返回未找到。修复没有重生字体或依赖版本变更；本地9.6源码下载曾超时，因此当时只报告9.5验证。后续beta.35手动CI使用LVGL9.6.0~1完成构建与全48项回归，包含真实启动器名称检查。设备显示仍待用户更新验证。

## 遥控台第3版

鼠标页使用留白触控区、独立滚轮、圆形左右键和明确的拖拽锁定。原`GAIN/TAP_MS/TAP_MOVE/SEND_MS`、鼠标四字节Report及键盘/媒体Report保持原约定；不改变坐标方向或用全屏滑动退出吞掉鼠标输入。演示计时、翻页和离线状态保留。

音乐主页面只有上一曲、播放/暂停、下一曲三个主按钮，仍控制系统当前媒体目标。按2026-10-19追加反馈，三键中心统一在屏幕y=233；播放为112×112触点、40px图标，左右80×80；静态背景与边框透明，按下才有轻微反馈。**长按底部“音乐”**打开原有音量增减/静音，再次长按或返回可收起；不删除这三项已有控制。

`remote_media_get_snapshot()`只提供真实主机回传状态。`app_mouse::update_media_view()`在revision变化时把完整466×466 RGB565封面和350×180 A8文字遮罩复制到UI专属PSRAM；封面慢转、文字固定，暂停/遮挡不补算动画。文字保持原协议，前130行歌词放在控件下方、尾50行曲名歌手放在上方；无歌词时前130行本来是曲名歌手，改放上方并隐藏尾段。两段共享UI自有缓冲，不增加传输或另一份像素内存。图层内存不足或复制版本已失效时不发布错误图像。退出先删除LVGL对象/三个图像描述符的缓存，再释放UI内存。

没有桥接时三键仍可用，显示“等待电脑同步曲目信息”和播放入口，不根据点击猜测真实暂停状态；仅在主机明确回传播放状态时改为暂停图标。无封面、换曲、断连或桥接心跳失效清除旧图；无匹配本地歌词时显示曲目信息。歌词不会自动联网查找：Mac工具支持显式绑定曲名的本地LRC。

本轮居中三键的原生预览位于`artwork/remote/native/centered-controls/`，歌曲/封面/歌词仍为合成输入；不能替代真实Mac桥接验证。相关回归检查三键位置、透明底、播放大小、原六种媒体用法、文字裁分/无歌词/隐藏释放与固定文字层。

### 蓝牙媒体能力边界

传统音响/车机的可选封面能力采用经典蓝牙AVRCP的BIP/OBEX Image Pull；标准媒体属性没有同步歌词字段，双方仍需实现封面能力，不能假定所有蓝牙音响都有封面或歌词。[Bluetooth SIG AVRCP 1.6.3 §5.14及Appendix E](https://files.bluetooth.com/wp-content/uploads/dlm_uploads/2024/10/AVRCP_v1.6.3.pdf)。当前ESP32-S3只有BLE、没有Classic，不能直接换软件栈复制该路径。[Espressif芯片能力表](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/bt-architecture/overview.html)。

Apple Watch Now Playing是Apple设备间的系统能力，官方说明不构成通用配件接口保证；公开BLE AMS面向iOS，曲目字段只有artist/album/title/duration，没有封面或歌词，也不是已验证的Mac替代方案。因此保留现有手动Mac本地桥接，先验证真实曲目/封面链路，不引入联网歌词、换硬件或假数据。[Apple Watch](https://support.apple.com/guide/watch/now-playing-apd4ea5db227/watchos)、[AMS范围](https://developer.apple.com/library/archive/documentation/CoreBluetooth/Reference/AppleMediaService_Reference/Introduction/Introduction.html)、[AMS字段](https://developer.apple.com/library/archive/documentation/CoreBluetooth/Reference/AppleMediaService_Reference/Appendix/Appendix.html)。核对日期2026-10-19。

### 本轮修复与验证（beta.35）

- 本地ESP-IDF 6.0.1 / LVGL 9.5固件构建通过，9项HID、遥控UI、真实启动器名称与启动/回滚定向测试通过；新增两处C测试的GCC15严格检查通过。源码与Mac自测通过不代表已安装新固件。
- 用户授权手动Mac桥接实测，真实Now Playing流随后给出60638字节封面；起初单次`get`无封面不能当作永久缺图。未读取到歌词字段，当前仍仅支持显式绑定的本地LRC。
- 真实已连接soRound在CoreBluetooth的HID/media服务查询中不可见，Battery/DeviceInfo查询各返回同一设备。Mac端修正为合并去重、仅唯一匹配设备自动连接，保留`--device`与未连接时HID扫描；未改固件广播或配对。
- 修正后已到自定义服务发现和加密STATUS读取ready，但写入报`Peer failed to respond to ATT value indication`，没有封面或文字层的COMMIT ACK。传输仍未闭环，不能宣称屏幕已显示、真实吞吐通过或判断一定是MTU问题。按本轮两次行为修复上限停止追加修改；下一步先记录失败opcode/长度、错误域码与两种写入长度上限，再区分长写与设备响应问题。细节与诊断入口归[Mac桥接说明](../mac-companion/README.md)。
- 用户先选择“暂不写设备，先保留修复”，随后另行明确要求发版本，修复源码`f9c6bf4`已发布为beta.35。本轮仍未读取USB设备、未烧录或迁移；设备固件与分区维持原状态，录音迁移审查见[CAPSULES.md](CAPSULES.md)。手动桥接及其子进程已停止，临时测试与下载文件结束时清理；发布包校验见下方记录。

原生截图`artwork/remote/native/`来自真实App与LVGL；HID连接、封面和歌词是明确的测试输入。`remote-media-lyrics-fixture.png`使用合成渐变及CoreText测试文字，**不是电脑正在播放的歌曲，也不是真机截图**。Lucide遥控页30px图标的原SVG/ISC许可位于`artwork/remote/icons/`，转换入口`tools/gen_remote_icons.py`。

## 验证和发行边界

- 主机测试：`tests/host`，含原输入释放/OTA/启动/天气/木鱼、录音五项与新旧图标适配；注册校验明确为23项并保留`APPS[19]==app_pixels`。
- 生态箱/专注：`tests/companion_apps`；真实状态流、NVS错误和原生双语渲染。
- 媒体解析与GATT：`tests/remote_media/run.sh`；真实解析器的ASan/UBSan边界验证。
- Mac：`mac-companion/build.sh`和产物`--self-test`；初次只读Now Playing探测曾返回`null`，后续本轮实测已能取得真实封面并连接加密服务，但ATT写失败、零图层提交ACK，仍未验证实际显示或封面吞吐。
- 离线录音分区：`tests/capsule_prepare_tests.py`；真实SDK生成镜像，但没有访问设备。

2026-10-19最终本地复核：主机47项、生态箱/专注2项、离线录音镜像3项全部通过；媒体ASan/UBSan与Mac自测通过。ESP-IDF 6.0.1完整固件构建成功，镜像`0x4cd3d0`字节，8MiB代码槽余40%。[原生界面合览](artwork/companion-apps/native/companion-overview.png)中的歌曲与录音状态为测试输入。

候选使用双8MiB代码槽；旧4MiB槽无法安装约5MB镜像。新录音布局保持只读表盘资源与独立可写卷，旧FAT必须在完整备份真实文件后重建，**不能截断或直接对现有raw FAT尝试RW挂载**。尚未迁移的设备不具备录音保存条件，明确报“存储未配置”。

已与A套启动器图标统一发布为[v1.7-beta.34](https://github.com/soBigRice/soRound_os/releases/tag/v1.7-beta.34)，源码`f447230`，标签CI39265622020通过发布依赖构建、47项主机回归、2项生态箱/专注、媒体ASan/UBSan与14项镜像测试。完整分发与存储兼容性见[发布记录](./PORTING_NOTES.md#2026-10-19-v17-beta34-实心图标与随身工具)。本地编译、主机渲染与协议测试均不替代真实BLE、触摸、音频、掉电恢复、帧耗时或用户审美验收。

字库和三键修复后续发布为[v1.7-beta.35](https://github.com/soBigRice/soRound_os/releases/tag/v1.7-beta.35)。手动CI39280665781全部通过；GitHub故障期间标签流水线39284415656的整体状态延迟，实际Release已生成，R2/国内完整包、条件Range与稳定包保护已独立核验。准确边界见[beta.35记录](./PORTING_NOTES.md#2026-10-20-v17-beta35-启动器字库与音乐控件)。
