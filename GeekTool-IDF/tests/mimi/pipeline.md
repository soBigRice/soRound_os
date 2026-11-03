# 完整小咪后台准备与验证

核对2026-11-03，ESP-IDF6.0.1 / LVGL9.5.0。本说明只覆盖纯准备器、后台所有权和主机整帧证明；真机帧率、任务栈与最终烧录状态由`MIMI.md`及设备回执记录。

## 调用与所有权

`app_mimi.c → mimi_pipeline_update(p, current_pet, &new_frame) → worker: mimi_assets_load() + mimi_prepare_eyes_rows() → done队列 → UI发布完整slot → mimi_render_select()`。

- UI宠物继续按原可见时钟推进。提交时复制`mimi_pet_t`，只将副本`motion_ms`量化到素材50ms姿态；UI状态不交给worker修改。身体资源、运动混合权重和眼睛轨迹使用同一`pose_ms`，普通眼睛轮廓、注视、眼睑仍取实时快照。
- 两块`mimi_pipeline_frame_t`在PSRAM，包含资源slot、A8双眼、对应快照与epoch/serial。UI拥有active，worker只写另一块。单job/单done队列传递完整结果及内存可见性，且只允许一个pending job。
- 同一clip/epoch的迟到完整帧仍可发布，再提交最新状态，避免读帧超过50ms后永远追赶最新index而饿死。相同epoch/index及完整量化快照不重复提交；比较时单独替换motion_ms语义，其他字节仍完整比较，不增加大型UI栈副本。`new_frame=true`是释放前一个显示slot的边界；调用方不得再解引用旧slot。
- 换clip、同clip时钟回退或显式`invalidate()`使旧完成结果失效。失效保留显示缓冲，在下一次成功发布前可用于遮挡冻结。FILE只由worker做load/fseek/read；UI的find只读已验证目录，join后才允许close。
- 读取/解压/准备失败锁存errors，停止本次会话反复重试；App保留完整矢量回退。该机制不是忽略错误或宣称回退具有目标帧率。
- 退出先解除渲染引用，再发STOP。worker报告栈余量后自行挂起，UI观察`eSuspended`后删除任务、关闭FILE并释放队列/帧/差分数组。1秒超时保留全部所有权并返回false，之后可重试destroy。CPU max锁仅包围读取和准备，不在空闲常驻。

## 可复现验证

从工程根目录运行：

- `python3 tests/mimi/prepare_tests.py`：冻结的原完整遮罩数学与逐行优化比较，普通与ASAN/UBSAN各5,992组完整/行API对照，覆盖14动作764姿态、三段彩环每姿态全部25眼、进入/退出fade、注视/眼睑/插值轮廓；另400次并发及无效参数/提供者失败。
- `python3 tests/mimi/pipeline_tests.py`：生产后台/准备器通过pthread FreeRTOS/PM/heap端口，资源IO为可控mock，分别运行普通、ASAN/UBSAN、TSAN。各260连续发布、780 active不可变检查、100动作切换、隐藏epoch、同clip重启、20退出状态、8分配失败路径、迟到发布、输入快照、错误锁存、超时保留和后续退出均通过。另100次相同量化快照早轮询不增加job或发布；快照内容、epoch或同clip重启发生变化仍提交。资源归零；模拟栈值只验证字段传递，不是真机栈测量。
- 先生成完整`MIMI.PAK`和manifest，再运行`python3 tests/mimi/build_full_ui.py`。使用`build/mimi-full-host`现有LVGL基线链接。普通与模块ASAN/UBSAN各372个原生帧比较（186×RGB565/交换格式），另450个动态眼睛/淡入淡出比较、28类完整矢量回退，以及半透明文字/ARGB叠加、隐藏不刷新与时钟冻结、恢复、父页删除、进退、原handler恢复。每格式另100次20ms早轮询验证不推进宠物时钟/长按、不重复flush，并能立即发布已经完成帧；原33ms状态门槛仍在40ms采样时推进。早轮询失败当次恢复完整矢量。LVGL基线本身未启用sanitizer，不能扩大声称整个LVGL都受sanitizer覆盖。

回执、源码摘要与原始日志位于忽略目录`build/flash-records/mimi-full-20261103/pipeline/`。脚本用临时目录编译并自动移除exe/obj/dSYM，不删除调用方的LVGL基线缓存。

## 本轮检查纠正

增加同clip重启用例时，TSAN发现测试在成功换帧后再次读取旧slot的epoch，违反了上述所有权边界。测试改为发布前保存epoch，再验证新帧；未通过放宽断言或关闭TSAN消除警告。最终三种后台构建均通过。后续测试与调用方都应在下一次`new_frame=true`前保存所需旧元数据，不能因旧指针尚未free就继续读取已交回worker的内容。

### 2026-11-03 第一轮调度跟进

完整设备首轮RTC已有效，但三个彩环低于17fps；统计显示worker在12秒窗口约有3.3～3.7秒未工作。launcher原20ms轮询外还有app33ms门槛与mimi_tick内部33ms门槛。只将小咪app周期设为20ms，并在内部elapsed<33时仅轮询/发布已完成三环帧，不推进原宠物、触摸和长按状态，也不重画未变普通帧。普通状态推进仍走既有33ms门槛，其他App/launcher/素材/缓存/renderer不变。backend增加上述快照去重，避免早poll用相同输入制造重复工作/帧率。主机验证不代表该跟进已达到设备目标帧率。

新UI用例最初在第二种像素格式等待完成计数超时：pthread端口的done观测只计本次账本第二个队列，上一格式生命周期使队列序号继续累加。各格式资源归零后重置端口分配/队列账本解决，未放宽等待或输出断言，也未请求重复快照作业规避去重契约。
