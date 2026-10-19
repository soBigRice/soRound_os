# 生态箱与专注唱片机

核对日期：2026-10-19。用户已选定视觉方向并授权实现及少量 NVS 状态；本模块不修改分区、不访问设备、不迁移或烧录。当前实现已通过本机状态测试及真实 LVGL 双语渲染，ESP32 上的触摸、音质、帧耗时和主观观感仍待设备验收。

## 入口与行为

| 模块 | 入口与状态 | 已实现行为 |
| --- | --- | --- |
| 生态箱 | `main/app_garden.c:app_garden` | 点盆景或“浇水”产生落水与水面涟漪；右侧按钮按自动 / 日间 / 夜间循环。自动模式在系统时间有效时按本地 07:00–19:00 显示日景，未校时默认日景。 |
| 专注唱片机 | `main/app_focus.c:app_focus` | 固定 25 分钟，中央开始 / 暂停 / 继续，右侧重置，左侧切换音景开关；点唱片或音景名依次切换雨声 / 海浪 / 篝火。 |
| 成长 | `main/garden_state.c` | 每完成一次专注累计 25 分钟与一片新叶；前六次在树冠上显现小芽，之后继续累计数值。不会因离开、漏浇水或关闭设备惩罚植物。 |

`focus_state_remaining_us()` 使用 `esp_timer_get_time()` 的单调时间计算，避免依赖 tick 次数或墙上时钟。暂停保存已耗时；恢复从新单调时间基准继续。到期转入 `FOCUS_DONE`，`focus_state_take_completion()` 只返回一次完成事件；`garden_state_complete_focus()` 再按递增 session ID 去重。

```text
启动 Focus → 读取偏好 + 共享 garden 工作副本 → READY
点开始 → RUNNING → scene_start(雨 / 海浪 / 篝火)
  点暂停 → PAUSED + scene_stop → 点继续 → RUNNING
  到 25:00 → DONE → alarm_once → scene_stop
           → garden_state_complete_focus(id, 25, day_key)
           → mark_dirty → NVS commit → 成功后显示新叶已保存
  点重置 → 先结算已到期会话，再恢复 READY
离开 App → 停止会话与本 App 音景，保存待写入状态
```

锁屏和快捷面板只暂停纯视觉：`app_focus.tick_in_background=true` 继续检查单调截止时间，隐藏时不旋转、不更新标签；音景继续。`app_garden` 隐藏时停止 tick。恢复时不补放积压动画。退出后两 App 清空对象引用，界面对象由启动器统一删除；不另开绘制任务、计时器或后台服务。

## 状态与失败边界

`garden_state_load/save()` 在独立 `garden` NVS namespace 的 `state_v1` 保存固定 32 字节、有 magic/version 的记录：累计分钟、专注次数、浇水次数、今日日期 / 分钟、最后结算 ID、昼夜设置。首次缺失视作默认数据；损坏、读取或提交失败均返回 false，不能冒充保存成功。只有完整读取校验成功后才替换工作副本；读失败时生态箱禁止写入，保留错误和重试入口。

`garden_shared_state()` 是两个 App 的唯一内存工作副本。写入失败保留 dirty 与内存进度，10 秒间隔重试，离开时再试一次；成功 commit 后才清 dirty。设备断电前仍未成功保存的进度无法保证恢复。时间未校准时显示“累计专注”，不伪造“今日”日期；取得可信日期后才切换今日计数。浇水每次回应，但计数与持久写入在本次进入期间按至少十秒间隔限流。

`focus_preferences_load/save()` 使用独立 `focus/prefs_v1` 保存音景和静音两项。偏好与成长使用各自重试时间，避免一项失败使另一项永远得不到重试。偏好提交失败时，跨页面进入仍保留未保存工作副本。正在运行的会话只跨锁屏继续，退出或设备重启后不恢复倒计时。

音频复用 `main/audio_out.h` 的同一 worker：`audio_out_scene_start/stop()` 只管理音景；完成先请求 `audio_out_alarm_once()` 再撤音景。Focus 不调用全局 `audio_out_deinit()`，以免撤销其他提醒。系统静音遵循既有设置；环境声不可用时计时继续并显式提示。环境声是离线合成声音，不宣称实地录音。

## 素材与渲染

图稿只作为视觉参考；实际界面的文字、计时、按钮、提示、昼夜、成长与交互均由 LVGL 实时构造。盆景和无字唱片是独立生成的位图，不是整屏 UI 图片。

来源、生成提示词、原图与设备 PNG 见 [`artwork/companion-apps/README.md`](artwork/companion-apps/README.md)。运行 `python3 tools/gen_companion_artwork.py` 从保留原件重建两个 `main/*_artwork.inc`。设备内嵌 `RGB565A8`：盆景 334×238 / 238,476 B，唱片 256×256 / 196,608 B，合计 **435,084 B**。A8 外底保留有效内容，避免夜间 recolor 将黑底染成矩形，也避免旋转方图擦掉邻近按钮；不会在设备上反复解码、创建位图或读取外部文件。

可复用边界经验：素材参与 tint / 旋转前，必须在实际 LVGL 的透明、遮挡与局部重绘路径检查外边界。仅看原图黑底与 AMOLED 背景一致不能证明变换后仍一致。当前测试分别断言夜景外底保持黑色、旋转后侧面按钮像素不变。

生态箱底部的排版必须按字体 `line_height` 验证，而不是按名义字号或肉眼字形高度估算。最终累计行采用 `font_companion_20` / y343（30 高），提示行采用 `font_companion_17` / y373（27 高），按钮保持 y400；像素占据范围依次为 343–372、373–399、400 以后。真实双语全部生态箱状态均断言两行以及两按钮在纵向不相交，防止文字字形完整但控件实际贴边覆盖。

## 验证与续接

独立测试入口：`tests/companion_apps/CMakeLists.txt`。仅在临时目录生成构建和 PPM，不注册到主机上长期运行；保留的精选 PNG 是原生审阅证据，不是设备验收。

```sh
cmake -S tests/companion_apps -B /tmp/wxesp32-companion-tests
cmake --build /tmp/wxesp32-companion-tests -j 4
ctest --test-dir /tmp/wxesp32-companion-tests --output-on-failure
```

- `state_tests.c`：精确到期 / 到期前一微秒、暂停继续、长时间隐藏后到期、完成仅一次、重置、递增 ID、日期切换、未校时、NVS 缺失 / 损坏 / 读写 / commit 失败及句柄关闭。
- `ui_tests.c`：实际 LVGL 双语所有可见字形与圆形边界、生态箱两行及按钮纵向分离、浇水 / 夜景、遮挡冻结动画、暂停保留计时、切音景、后台到期且仅一次增长、偏好和成长同时提交失败再重试、音频不可用仍计时、退出停止音景及透明边界像素。

临时构建、PPM 和日志在本轮结束时清理；固件整体构建与入口注册由同轮主任务整合。测试不连接板子，不进行烧录、发布、Git 提交或推送。后续设备验收重点：三音景实际扬声器效果与闹铃抢占、锁屏继续、25 分钟完成一次、NVS 断电保存、旋转时内存 / 帧耗时和圆屏触摸。
