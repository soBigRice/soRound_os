# v1.8 · soRound OS 正式版

将 v1.7-beta.38 的已发布功能晋升为正式版，固件生产代码、分区和依赖声明保持一致，以 v1.8 标签重新构建。正式与内测 OTA 通道同时更新至 v1.8。地图 App 和未提交的演示模式不包含在本版中。

## 更新内容

- 表盘由30款扩展为62款，保留 TYPE、ORBIT、SHIFT、HAND、STUDIO 五组及原有编号和已保存选择。新增32款：6款图形、8款设计、6款字体、6款运动图形和6款几何读时；均提供正常、常显及原生预览。
- 启动器增加独立“表盘 / Watchfaces”入口，与设置页复用同一选择器，支持左右循环、分组、保存及返回。原23个应用顺序保留。
- STUDIO九款采用已确认的纯黑背景；优化图形扫描范围、字形裁剪和重复绘制，按需缓存几何路径，分配失败沿用原计算。保留分辨率、采样精度、已有动效周期和旧款显示。
- 设置、生态箱与专注数据通过后台队列合并保存，退出时有界等待；失败保留待保存状态。操作与实际落盘之间存在短暂断电窗口，不承诺断电零丢失。减少静止页面的重复刷新，实际渲染时启用CPU频率锁，保留空闲、常显及熄屏策略。
- 优化相关控件、声音胶囊与遥控台的静止刷新，保留原交互、存储及音频能力。
- 官网中英文图鉴同步62款原生预览，区分本次新增32款与原有30款，下载入口更新为本版。

## 发布验证

- [v1.8发行CI40666709157](https://github.com/soBigRice/soRound_os/actions/runs/40666709157)全部通过：ESP-IDF6.0.1 / ESP32-S3、LVGL9.6.0~1构建，59项主机回归、2项生态箱/专注、媒体ASan/UBSan与14项OTA镜像检查。
- 应用5,462,736B，内嵌版本为`v1.8`，SHA256 `84e36a8912cd6b91b43287a3e98f0b00a2292c62724ebcd00172c532916d4a8a`。精确CI包与GitHub完整下载及资产声明摘要一致；R2与设备OTA地址的正式/内测四个完整包及条件Range均核对通过，详见[发布记录](https://github.com/soBigRice/soRound_os/blob/main/GeekTool-IDF/PORTING_NOTES.md)。
- 前序 beta.38 的[发行CI39736804822](https://github.com/soBigRice/soRound_os/actions/runs/39736804822)与分发记录仅作为历史证据，不代替 v1.8 重新构建和校验。

## 验证与升级边界

- 标签发布流程执行 ESP-IDF6.0.1 / ESP32-S3 构建，并在实际发行LVGL依赖下运行完整主机、生态箱/专注、媒体ASan/UBSan和OTA镜像检查；最终执行结果以本版Actions为准。
- 此前功能候选的设备安装、全屏/选择器/AOD与生命周期验证分别记录在[表盘说明](https://github.com/soBigRice/soRound_os/blob/v1.8/GeekTool-IDF/WATCHFACES_UI.md)。这些历史候选记录不等于本次精确CI包的设备或网络OTA验收。本轮发布不访问或写入设备。
- 本包要求双8MiB应用槽。普通应用OTA不会扩容分区、迁移或格式化资源与录音卷；旧3MiB/4MiB布局不能直接安装超出槽容量的包。
- 运动图形、太阳、潮汐等为读时设计，不提供心率、步数、GPS或真实日出/潮位数据。实际帧率、功耗、长时间并发、主观观感及故障自动回滚仍需分别验证。
- 原有限制保留：电脑三键使用标准BLE HID；封面/文字回传存在ATT实测失败，歌词仅显式绑定本地LRC；声音胶囊需要单独初始化录音卷。

**beta.36继续撤回，请勿安装。** 双8MiB布局的设备可在OTA页面直接检查正式更新，无需开启测试通道；旧布局须先按分区说明迁移，应用bin不能代替首次完整烧录。

[表盘逻辑](https://github.com/soBigRice/soRound_os/blob/v1.8/GeekTool-IDF/WATCHFACES_UI.md) · [后台保存与随身工具](https://github.com/soBigRice/soRound_os/blob/v1.8/GeekTool-IDF/COMPANION_APPS.md) · [录音迁移边界](https://github.com/soBigRice/soRound_os/blob/v1.8/GeekTool-IDF/CAPSULES.md)
