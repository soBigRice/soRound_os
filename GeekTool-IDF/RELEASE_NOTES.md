# v1.7-beta.34 · 实心图标与随身工具更新

## 本版变化

- 启动器统一为A套黑白实心图标，覆盖原20项和新增3项应用；校准大小、视觉重心与切换坐标。
- 新增生态箱、专注唱片机与声音胶囊；专注完成可积累成长，录音支持本地保存、列表、回放及删除。
- 遥控台更新为大触控区、独立滚轮、拖拽锁定和三键媒体控制；封面与文字采用独立图层。
- 提供手动启动的Mac本地媒体桥接源码。系统曲目信息、封面和显式绑定的本地LRC只经加密BLE传输，不走云端、不自启动。
- 合并此前已提交的天气绘制、RAM/PSRAM分配和Flash容量改进；流体保留粒子模式，移除慢染料模式。

## 升级与存储兼容性

- 本版使用双8MiB OTA应用槽。旧3MiB/4MiB槽的设备须先进行USB分区迁移，普通OTA不能扩大分区；设备会按运行时槽容量拒绝过大的固件。
- 已有双8MiB应用槽可安装固件；声音胶囊另需独立、已初始化的录音分区。旧布局下显示“存储未配置”，不会自动格式化。
- 启用录音分区前，须备份设备真实文件，再迁移分区和恢复表盘资源。仓库的默认图片不能代替设备备份；本次公开发版不执行任何设备烧录或迁移。
- 固件下载为应用镜像；迁移工具只生成离线计划与镜像，不连接设备或自动写入。

## 验证边界

这是内测版。CI使用发布依赖执行构建、启动/回滚、原生图标、UI、录音存储保护、专注/生态箱状态和媒体协议检查。
真实AMOLED观感、触摸、录音/扬声器、BLE封面吞吐、不同播放器及实际分区迁移仍需设备验证。
Mac桥接使用系统私有媒体接口，尚不承诺所有播放器兼容；本地未提供LRC时不会伪造歌词。

正式通道保持v1.7；本次仅更新beta通道。

详细使用与边界见 [声音胶囊](https://github.com/soBigRice/soRound_os/blob/v1.7-beta.34/GeekTool-IDF/CAPSULES.md)、[生态箱与专注](https://github.com/soBigRice/soRound_os/blob/v1.7-beta.34/GeekTool-IDF/COMPANION_GARDEN_FOCUS.md)、[Mac本地桥接](https://github.com/soBigRice/soRound_os/blob/v1.7-beta.34/mac-companion/README.md)、[启动器图标](https://github.com/soBigRice/soRound_os/blob/v1.7-beta.34/GeekTool-IDF/LAUNCHER_ICONS.md)。
