# Mimi 只读素材包 / Read-only asset pack

格式版本1，核对2026-11-04，ESP-IDF6.0.1、LVGL9.5.0。实际交付使用6,361,744字节原始span表示，SHA256 `fea8e0c7140c946b414bdf2ad59079b7de2f3deef10c5c2b8934fa7aea21d42c`。canonical实验虽通过主机像素/读取检查，真机加速完成数为0，未形成有效性能结果，已撤销设备安装；不得作为生产默认素材或宣称性能通过。

The delivered package uses the original span representation. The canonical experiment passed host checks but failed target optimized-path coverage and was reverted; it is not the production default.

当前只定义小咪私有新文件 `MIMI.PAK`，不改变原表盘或录音格式。所有整数为小端；版本、保留位、长度、CRC或完整帧目录不符时拒绝文件。实际安装由卷工具在现场卷副本追加，素材生成器不访问设备。

Format1 uses little-endian integers and a private new `MIMI.PAK`; existing watchface/audio formats are unchanged. Invalid versions, reserved fields, lengths, CRCs or frame catalogs are rejected. Generation does not access a device.

## 64字节包头 / 64-byte header

| 偏移 / Offset | 内容 / Contents |
| --- | --- |
| 0 | 8字节 `MIMIPAK\0` / eight-byte magic |
| 8,12,16 | u32版本1、包头64、帧数186 / version, header bytes, frame count |
| 20,22,24,26 | u16宽466、高466、采样50ms、流数5 / width, height, period, stream count |
| 28,32,36,40 | u32目录字节数、文件字节数、目录CRC32、渲染版本1 / index bytes, file bytes, index CRC32, render revision |
| 44 | 16字节零保留 / sixteen zero bytes |
| 60 | 前60字节CRC32 / CRC32 of the preceding60bytes |

目录恰好186×64字节，Play=10/50帧、Orbit=11/78帧、Comet=13/58帧，每段从0起间隔50ms，含完整淡入淡出。`mimi_assets_find()`以可见宠物时钟向下量化，超出末帧时有界取末帧；不修改宠物本身的时钟或普通表情。

The catalog contains complete Play/Orbit/Comet clips including fades. Lookup floors the visible pose time to50ms and clamps to the final sample without changing the pet clock or live expression state.

## 每帧64字节目录 / 64-byte frame entry

| 偏移 / Offset | 内容 / Contents |
| --- | --- |
| 0,4,6,7 | u32正文偏移、u16时刻ms、u8动作ID、u8零保留 / payload offset, pose time, clip, reserved |
| 8 | 五个u32原始长度 / five raw lengths |
| 28 | 五个u32压缩长度 / five compressed lengths |
| 48 | 五条原始流依次累计的CRC32 / CRC32 accumulated over all five raw streams in order |
| 52 | 12字节零保留 / twelve zero bytes |

五条流顺序为 `base_rows/span_rows/body_rle/base_rle/spans`，各自采用Raw Deflate（无zlib头）。一帧五个压缩流紧接排列，帧末补0至4字节边界；帧与帧连续，文件不允许额外尾随数据。空span流必须为标准空Deflate `03 00`。CRC使用zlib兼容种子0，与 `esp_rom_crc32_le(0,...)`一致。

Each frame stores five independent Raw Deflate streams, followed by zero padding to a4byte boundary. Frames are contiguous and trailing data is rejected. Empty span streams use canonical `03 00`. CRC seeds are zlib-compatible zero.

行索引为467个u32偏移。身体为466×466的A8 RLE，底图每行独立RGB565 RLE；RLE token为u16，低15位数量，高位表示重复值，否则为字面量。span为8字节 `(u16 x,n,color; u8 q,pad)`；q=1..32，pad=0，保留原生RGB565每次混合的舍入和顺序。不得合并为单层ARGB。加载时验证完整RLE输出、单行623条span上限及936字节底图RLE上限。

Row tables contain467u32 offsets. RLE uses a16bit token with a15bit count and high-bit repeat flag. Spans preserve native per-operation RGB565 rounding and order; they are not flattened to ARGB. The reader validates all row/RLE/span bounds before publication.

## 构建期前景规范化 / Build-time foreground canonicalization

当前builder默认启用优化revision1，但包格式仍为版本1，reader、runtime和画质参数均不改变。`front_canonical.c`逐像素展开前景操作：若存在q32，最后一次q32覆盖之前的背景和操作前缀无关；从该颜色开始按原`mix_q()`顺序逐次计算透明后缀，得到与背景无关的最终RGB565颜色，再以单个q32表示。没有q32的像素序列逐元素保留。按depth合回相邻同色/q的span只交换不同像素的独立操作，不改变同一像素自己的顺序。

Builder optimization revision1 retains format1 and changes neither reader/runtime nor quality settings. The final q32 makes all preceding operations and the background irrelevant. Its suffix is evaluated with the original per-operation RGB565 rounding and replaced by one constant q32. Sequences without q32 are preserved element-for-element; depth grouping only reorders independent pixels.

全帧盲重排会让部分透明帧变大，因此采用静态、可复现选择：必须同时存在q32、raw span字节不增加、五流压缩总量严格减小，且候选符合现有623条/行、416800字节解压帧和96035字节压缩帧上限。任一体积/容量条件不符就保留原spans与行表字节、顺序；数学/原生像素验证失败则停止生成，不能伪造通过。该选择仅发生于构建期，没有运行时降级或新增RAM缓存。

Blind regrouping can enlarge transparent frames. Static selection therefore requires q32 coverage, non-growing raw spans, smaller five-stream compressed payload, and all existing reader limits. Failed size/capacity guards retain the original span/row bytes and order. Failed correctness proofs stop generation. No runtime fallback or new RAM cache is introduced.

本素材恰有83帧采用候选、103帧原样保留，包6,361,744→4,884,924字节；`canonical-delta.json`记录逐帧选择和容量。原生186帧逐字节一致；每个前景覆盖像素用4固定色+4随机背景检查，每个含q32的帧最长原栈另外穷举65536种背景。普通/动态眼睛的背景属于同一RGB565域，因此证明不依赖默认眼睛。没有减少几何线段、AA或时间采样；字节/操作减量不是设备fps承诺。

Exactly83frames are selected and103keep their original representation. Every native frame remains identical, with fixed/random background checks and exhaustive65,536background checks for each applicable frame's longest opaque stack. The RGB565 proof also covers live eyes. Geometry, AA and temporal sampling are unchanged; data savings do not guarantee device fps.

## 运行时所有权 / Runtime ownership

`mimi_assets_open()`只读一次包头/目录；目录不变，可供UI查找与worker并行。worker独占FILE、4KiB内部输入和约11KiB内部ROM解压状态，串行加载到另一个slot；没有每帧堆申请。slot内生成466×4字节身体行索引，避免重扫RLE前缀，也避免把37KiB静态索引推入应用镜像。失败可能留下部分slot，必须整帧丢弃；完成后由队列发布，UI释放前禁止修改。join线程后才close reader。

The immutable catalog supports concurrent UI lookup, but only one worker owns FILE/input/inflater access. No target-frame allocation occurs. A body-row index is generated inside each slot. Failed partial slots must never be published, and closing requires worker shutdown first.

底图/前景不包含眼睛；眼睛继续使用实时25套轮廓、注视、眼睑和当前动作的量化姿态，在同一身体遮罩下裁切。普通表情和其他身体动作仍由原路径负责。

Eyes are absent from the pack and remain live. Their expression, gaze and eyelid snapshot is blended at the exact quantized body pose and clipped by that body's mask.

## 复现 / Reproduce

在工程根目录已生成 `build/mimi-full-host` 的 `mimi_ui_tests` 主机依赖后，执行 `python3 tools/mimi_assets/build_pack.py`，默认使用已验证原始表示并输出`build/flash-records/mimi-full-20261103/assets/`，遇到已冻结包即拒绝覆盖。`--out`可选新目录；仅在研究旧失败时显式加`--no-original-spans`复现canonical，不作为交付输入。生成器以 `MIMI_REFERENCE_ONLY`包含实际 `main/app_mimi.c` 的原始矢量路径，从真实LVGL捕获前景操作，并与原生全帧逐字节比对；Python逐帧压缩/验证后删除该帧原始文件。保留每帧参考SHA256/CRC32、遮罩/流摘要和源码指纹，不累积大图。`verify_pack.py --assets 新目录`核对实际C读取器的全186帧、86,676行身体与损坏输入，回执只写指定目录。

The generator includes the current vector renderer under `MIMI_REFERENCE_ONLY`, captures actual LVGL operations and byte-compares replay against native frames. Raw files are cleaned frame-by-frame; hashes and source fingerprints remain. The verifier checks the actual C reader and malformed-input rejection.

实际C读取器验证还检查每帧模块分配调用次数不变、读取期间5,000次并行目录查找、28类损坏拒绝及正常/ASAN/UBSAN构建。ESP分支的第三条分配失败在主机stub中单独验证；stub不证明解压能力。可选 `--tinfl-source /path/to/miniz-v114.c` 使用[上游公开源码](https://raw.githubusercontent.com/richgel999/miniz/v114/miniz.c)与当前SDK头检查同一4KiB流式包装，但仍不是真机ROM/帧率证明。验证脚本不联网或修改设备，临时编译目录自动删除。

Reader checks include stable per-frame module allocation counts,5,000concurrent catalog lookups,28malformed-input cases, and normal/ASAN/UBSAN builds. An ESP allocation-only stub is explicitly separate from optional host execution of the streaming wrapper with upstream tinfl; neither replaces target-ROM or performance measurement. Tests do not access a device or network and remove temporary builds.
