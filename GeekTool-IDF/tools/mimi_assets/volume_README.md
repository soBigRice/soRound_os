# 小咪资源卷工具 / Mimi resource-volume tools

本工具用本机 ESP-IDF 6.0.1 的 FatFs R0.15/patch2 原件读取既有卷，在内存副本上以 `FA_CREATE_NEW` 追加根目录 `MIMI.PAK`，通过独占创建写到另一个镜像文件。它不连接设备、不写输入镜像、不格式化、不修改已有文件、不转换 WL，也不增加运行时可写权限。
The tool uses the installed SDK's FatFs source, mutates an in-memory copy, and exclusively creates a separate output image. It never connects to a device or reformats a volume. `FF_USE_MKFS=0` removes formatting support from its host build.

从 `GeekTool-IDF` 运行；`--idf` 可显式指定已核查 SDK，默认使用 `IDF_PATH` 或 `~/.espressif/v6.0.1/esp-idf`。SDK 原件和临时可执行文件在临时目录构建，退出后清理，未安装额外依赖。
Run from the firmware project. The SDK sources and host executable live in a temporary directory that is removed afterward; no extra packages are installed.

```sh
python3 tools/mimi_assets/volume_tool.py inventory /private/old.bin --json /private/old-files.json
python3 tools/mimi_assets/volume_tool.py append /private/old.bin /private/MIMI.PAK /private/new.bin --proof /private/append-proof.json
python3 tools/mimi_assets/volume_tests.py --volume /private/old.bin --proof /private/volume-tests.json
```

输入必须是已有 `storage` 卷的普通文件快照，长度 `0x7e0000`；拒绝设备、坏几何或未知文件系统。输出必须不存在且与输入不同。已有 `MIMI.PAK` 即拒绝，不静默替换。底层 C 工具的 `cat` 命令仅供 Python 捕获并计算摘要，避免把用户图片内容写入终端。
Input must be an existing regular-file snapshot of the resource partition. Existing output/pack files, invalid geometry, and device inputs are rejected. The C `cat` operation is captured by Python for hashing; do not print private asset bytes to a terminal.

## 保全检查 / Preservation checks

`volume_tool.py` 在 SDK 完成追加后，用独立只读 FAT12/FAT16 解析及再次通过 SDK 读文件，核对：
The wrapper independently inspects the FAT structure and rereads every file through the SDK to check:

- 既有路径、尺寸、属性、首簇和 SHA256 均不变；原根目录活动项及 LFN 项逐字节不变。
- 原来已分配的数据/目录簇逐字节不变，对应 FAT 链及保留 FAT 项不变。
- 新分配簇恰好属于 `MIMI.PAK`，不触及原簇或无关空闲簇；新文件读回大小/SHA256 与输入一致。
- BPB、卷几何、保留区和无效尾部不变；FAT 镜像一致；原输入文件始终相同。

现场卷的 FAT sector 和 cluster 均为 **4096 字节**。回执同时列出 512 字节差异和 4096 字节 Flash 变更块；安装应使用完整、已审查的 4096 字节块，不将 512 字节统计直接当作擦写单位。回执的写入建议顺序为：新文件数据 → FAT → 根目录。根目录块包含既有项，掉电仍有窗口；这不是原子文件追加。设备写入、原卷备份及恢复由外层安装流程负责。
The observed volume uses 4096-byte FAT sectors/clusters. The receipt reports both diagnostic 512-byte changes and complete 4096-byte Flash blocks. Its suggested order is new data, then FAT, then the root directory; power loss is still possible during metadata updates. Device writes and recovery remain the installer's responsibility.

库存/回执可能包含私有文件名和摘要，只放在忽略的私有记录目录。测试默认仅使用提供的卷副本；正常和 ASAN/UBSAN 各覆盖11类检查，容量测试字节不是实际小咪素材。当前离线参考卷有3个原文件、空闲7,032,832字节；6,362,972字节测试负载分配6,365,184字节后余667,648字节。本轮真实写入前必须对新鲜现场卷重新核对，不能将这个历史快照当成现场刚读结果。
Inventories and receipts may contain private filenames and hashes, so keep them in ignored private records. The sizing payload is synthetic; the installer must independently verify the current device snapshot before writing.

## 共用只读挂载 / Shared read-only mount

生产入口 `main/resource_store.c::resource_store_mount()` 复用原 `/img` / `storage` raw FAT 只读卷，`max_files=2`、`format_if_mount_failed=false` 不变。静态 mutex 仅在短 `portMUX` 临界区内初始化；等待及 FAT 挂载均在临界区外。锁最长等2秒，失败不标记已挂载，下一次可重试。挂载成功后整机复用，不增卷或后台任务；录音卷保持原合同。
The shared entry keeps the existing read-only mount contract. Initialization is protected briefly, while mounting runs under a bounded blocking mutex outside the spinlock. Failed attempts remain retryable; successful mounts are reused.

`img_store.c::decode_image()` 调用该入口；既有图片缓存控制测试继续覆盖出厂图识别、自定义图优先、主题缓存/任务清理，并增加挂载失败回退。`tests/watchfaces/resource_store_tests.c` 编译真实挂载实现，用 pthread 服务替身验证12线程竞争、初始化/挂载失败后重试、缓存复用、临界区边界与2秒超时；普通、ASAN/UBSAN、TSAN 均通过。测试不替代目标设备挂载、资源读取与帧率验收。
The image-cache tests retain their existing behavior checks and add mount failure fallback. Dedicated tests compile the real shared mount with pthread service fixtures; device I/O and animation performance still require hardware checks.

来源 / Sources: [ESP-IDF 6.0.1 FAT 文档](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/api-reference/storage/fatfs.html), SDK `components/fatfs/src/ff.c`, `ff.h`, `diskio.h`, `ffunicode.c`。构建回执记录这些原件与主机配置的摘要，复制到临时构建目录时保留原许可证。
