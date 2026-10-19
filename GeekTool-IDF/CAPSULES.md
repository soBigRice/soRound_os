# 声音胶囊与共享音频

核对日期：2026-10-19。软件更新、录音存储初始化和真机录音分别验收；当前尚无录音分区迁移完成或声音胶囊录音/回放真机验收证据。

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

### 升级路径与“存储未配置”反馈

2026-10-19 用户反馈声音胶囊显示“存储未配置”。目前仅有界面反馈，未读取设备分区表或meta，
不能声称已证明设备具体缺少哪个分区、meta为空，或实际内容损坏。`capsule_store_mount()` 的该状态覆盖：
缺少 `capsule_meta` / `capsules`、地址或大小不符、录音分区加密，以及meta读取或字段/CRC校验失败。
两份WL状态或FAT挂载失败则返回另一状态，显示“需离线恢复”。

两种升级路径必须分别检查：

- **从旧布局普通OTA升级**：旧表只有 `storage=0x1020000/0xfe0000`，没有录音分区。
  `ota_update.c::ota_update_run()` 仅选择已有应用槽并通过 `.partition.staging` 更新应用；
  `.github/workflows/firmware.yml` 也仅发布 `GeekTool.bin`。
  因此仅OTA、从未迁移的设备仍保留旧分区表，不会创建录音分区，出现此提示是预期保护。
- **使用新表执行默认USB flash**：新表可以包含录音分区，但当前 `main/CMakeLists.txt` 仅将
  只读 `storage.bin` 加入 `FLASH_IN_PROJECT`。默认清单没有 `capsule_meta.bin` 和预初始化WL的
  `capsules.bin`；若这两者此前未单独受控写入，采用新表也不足以启用录音。
  不能把“整包烧录完成”当成录音卷已初始化，更不能用自动格式化掩盖这个缺口。

本次离线小复现用当前 `prepare_capsule_storage.py::meta_image()` 生成meta，直接交给当前
生产 `capsule_store_meta_valid()`：合法meta通过、空白meta拒绝。未发现生成器与固件guard格式/CRC不一致；
这项验证不代表设备上的meta已被读取或验证。

### 可审查的首次迁移步骤

1. **先形成离线计划**：已有完整且确认过的设备文件备份时，用当前SDK Python运行
   `tools/prepare_capsule_storage.py --assets-dir <完整备份目录> --output-dir <全新输出目录> --idf-path <SDK目录>`。
   该步骤只生成本地文件和SHA-256清单，不读写设备；没有真实备份时不能把仓库 `images/` 当成可执行的设备迁移来源。
   从当前 `partitions.csv` 离线生成分区表文件，与三份数据镜像一起审查目标范围：

   | 目标 | 起始地址 | 大小/边界 |
   | --- | --- | --- |
   | 当前表对应的分区表文件 | `0x8000` | 以生成文件长度为准，不跨入 `0x9000` 的NVS |
   | 重建并保留真实文件的 `storage.bin` | `0x1020000` | `0x7e0000`，结束于 `0x1800000`（不含） |
   | 预初始化WL的 `capsules.bin` | `0x1801000` | `0x7ff000`，结束于 `0x2000000`（不含） |
   | `capsule_meta.bin` | `0x1800000` | `0x1000`，必须最后写入 |

2. **另行获准后才连接和备份设备**：读回实际分区表，保留原完整资源分区及真实文件，
   与离线来源逐一核对。确认文件可完整放入缩小后的资源卷；旧FAT不能直接截断。
   如果设备已经存在有效录音卷或录音文件，应停止首次初始化方案，另审保留/恢复路径，不能覆盖为新空卷。
3. **仅按审查后的范围受控迁移**：分区表、重建资源卷和预初始化录音卷逐项写入并核对读回；
   在这些内容验证完成后，最后写入并核对meta。保持NVS `0x9000/0x6000`、otadata `0xf000/0x2000`、
   phy_init `0x11000/0x1000`、两份OTA应用槽 `0x20000/0x800000` 与 `0x820000/0x800000` 及bootloader不变。
   当前应用如已更新，无需以迁移为由重写它；不使用整片擦除或未经审查的默认全量flash清单代替迁移计划。
4. **再做真机验收**：确认已有表盘文件仍在，录音可保存和回放，重启后列表保留，退出/取消和错误状态正常。
   软件构建、OTA成功、离线镜像生成和meta校验通过，均不能代替这些验收。

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
