# 声音胶囊与共享音频

核对日期：2026-10-19。当前为本地实现，未烧录、未迁移、未进行麦克风/扬声器真机验收。

`app_capsules.c` 只构建 LVGL 控件并读取 `capsule_recorder_snapshot()`。录音按钮发送请求，
`capsule_recorder.c::recorder_worker` 在独立任务中挂载、扫描、删除、录音和请求回放。
麦克风启动前通过 `audio_bus_acquire()` 获取 I2S0；采样为板载 ES7210 的真实 16kHz 单声道 PCM16。
波形取每个256样本块的绝对峰值，不使用演示波形。最多120秒、最多32条录音，保留64KiB空闲空间。

录音只写 `RECORD.TMP`，停止后回填44字节 WAV 头、flush/fsync/close，再重命名为8位十六进制 `.WAV`。
写入错误、退出或重新进入会取消未完成录音；下一次进入清理断电遗留的同名临时文件并提示。
达到预留容量边界会停止并保存此前完整写入的块，同时显示存储已满；实际短写仍取消文件，不把部分样本冒充完整录音。
无效 WAV 会在列表标记损坏，不能回放，仍可长按并再次点击确认删除。锁屏/遮挡请求停止并保存当前录音。
退出不阻塞 LVGL，由任务关闭文件、停止硬件、释放音频总线并卸载卷后释放任务栈。

回放由现有 `audio_out.c::output_worker` 执行，仅读规范 `/caps/XXXXXXXX.WAV`；
校验固定 PCM16 WAV 头、总长度、采样率、声道和120秒上限，以512样本块读取。
录音与回放不同时持有 I2S0。闹铃可抢占回放；被抢占的回放取消，不自动恢复。
雨、海浪、篝火为代码合成环境音，不是真实环境录音；共用同一个输出任务、静音和音量规则。
`audio_out_scene_stop()` 只撤销音景意图，闹铃优先且不被该调用停止；音景在闹铃结束后恢复。

## 存储保护和迁移边界

原 `storage` 仍用 `img_store.c` 的只读 raw FAT；绝不改为 RW+WL。
双8MiB OTA分区和 NVS 地址不变。新表将资源分区缩至 `0x1020000 / 0x7e0000`，
独立 `capsule_meta` 为 `0x1800000 / 0x1000`，可写 `capsules` 为 `0x1801000 / 0x7ff000`。
旧FAT不能截断后直接继续使用，必须先备份真实文件，再重建小容量镜像并恢复所有文件。

正常启动或进入App**绝不格式化**。`capsule_store_mount()` 先只读检查分区地址、大小、
meta magic/schema/CRC/4096扇区/WL v2绑定，再验证两份WL状态头CRC与版本及边界。
只有通过检查才调用官方 `esp_vfs_fat_spiflash_mount_rw_wl()`，且 `format_if_mount_failed=false`。
缺分区或meta失败显示“存储未配置”；WL或FAT失败显示“需离线恢复”，拒绝录音。
这里额外校验WL很必要：SDK `wl_mount()` 即使关闭FAT自动格式化，仍可初始化不合法WL元数据。
两份WL状态有一份损坏也拒绝挂载，保留离线恢复机会，不自动修复或重建。

meta格式：前8字节 `GTVCAP01`，其后小端32位 schema=1、capsules地址、大小、扇区=4096、
WL版本=2、前28字节CRC32(seed=`UINT32_MAX`)。余下字节全为 `0xff`。

`tools/prepare_capsule_storage.py` 只生成本地 `storage.bin`、预初始化WL的 `capsules.bin`、
`capsule_meta.bin`、含源文件和镜像 SHA-256 的迁移计划，无串口、格式化设备或烧录入口。
用当前SDK Python环境执行，并显式指定完整备份目录；输出目录必须全新。
仓库 `images/` 只可用于离线测试，不能代替真实设备备份，也不能证明设备上没有用户文件。
capsules 不自动加入 `FLASH_IN_PROJECT`，普通构建不会生成或写入录音卷。
**任何烧录、设备备份/读取或迁移都需按本次用户约定另行确认。**

官方依据（本地SDK 6.0.1已核对）：
[FatFS与离线生成器](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/api-reference/storage/fatfs.html)、
[磨损均衡](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/api-reference/storage/wear-levelling.html)。
具体格式依赖SDK v2 WL布局；升级SDK时必须重新核对 `WL_Flash.cpp` 和 `wl_fatfsgen.py`，不直接放宽检查。

## 本地验证与待验收

`tests/capsules.cmake` 接入五项 host 测试：真实WAV/PCM往返、短写/容量/取消；
生产挂载路径的meta/WL拒绝与原storage隔离；实际共享音频worker的闹铃抢占与取消；
实际录音worker的峰值波形、退出取消、容量停止保存和资源释放；原生LVGL中英文字形、圆屏边界、录音/回听/两步删除/返回。
`tests/capsule_prepare_tests.py <IDF_PATH>` 用指定SDK生成真实离线镜像，核对源文件和镜像哈希、meta与双WL CRC，并拒绝危险输出路径。
旧 `merit_audio_tests` 对告警、提示音、木鱼、连续敲击、idle DMA与退出仍通过。
原生截图保存在 `artwork/companion-apps/native/capsules-*.png`；截图录音状态为测试fixture，不是真机录音证据。

这些检查不等于真机录音/扬声器效果、实际分区迁移、掉电恢复或主观UI验收。
下一步必须先审查真实设备备份和迁移计划，再按用户另行授权执行烧录；随后验证录音与回放、重启后列表、错误提示及已有表盘文件。
