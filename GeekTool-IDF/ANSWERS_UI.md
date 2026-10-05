# 答案之书

核对日期：2026-10-05。新增独立App，追加在启动器原16个App之后；原App顺序、滑动切换、返回和电量环沿用。
字体、原生预览和验证对应本轮联网修订；LVGL9.5/9.6主机回归均17/17通过，ESP-IDF6.0.1本地构建通过，
beta.22的CI发布包、R2与国内完整包及Range已核对一致（详情见PORTING_NOTES），
设备联网、实体键、触摸与锁屏恢复仍待真机验收。

## 页面与数据

`main/app_answers.c` 提供书封和答案两种视图。点击书封或“翻开一页”进入答案；
点击答案区域或“再翻一页”继续翻阅；用于秒表/倒计时的右上PWR实体键同样翻页。
顶部返回键/右滑由启动器处理，直接回到启动器；BOOT锁屏/长按关机和其他计时App行为沿用。
书封为原生LVGL几何控件，白色边界、红色书签；答案使用独立32px平滑字体，底部操作使用24px字体。
布局按466×466圆屏组织；内容居中换行，按钮位于圆屏安全区域，不改变其他App的排版。

联网时每次接受翻页都会通过 `answers_fetch_begin` 请求新的中英文答案，语言跟随系统设置。
使用[小小API官方答案之书接口](https://xxapi.cn/doc/answers)，只发送`Next page`和一次性随机数；
没有问题输入，也不发送设备身份、Wi-Fi凭据、私人内容，不申请付费账号或密钥。
本轮实际HTTPS请求返回200及合法`title_en/title_zh`，没有把桌面请求视为设备联网验收。
选择它是因为公开接口提供简体中文/英文短标题；另核对的answerbook.david888.com接口偏繁体，未选用。
核查日期2026-10-05，官方文档/服务条款已读；后续接口变动可能触发备用状态。
实测相同question连续三次得到相同标题和request_id，即使带no-cache仍相同；
换不同随机附加值后得到不同标题。因此每个HTTP任务生成新的64位随机附加值，避免固定请求持续返回旧答案。
这些随机数不是设备身份或持久化数据；重复答案仍由UI明确转备用。

`main/answer_messages.h` 保留48组原创短句作为断网/获取失败时的备用，离线编译到Flash。
`esp_random`仅用于娱乐性抽取；同次翻页中的重复点击忽略，已显示的答案不会被迟到响应替换。
网络答案重复、缺少可显示字形或超出三行布局时，分别显示明确的备用原因；不截断有效内容或显示方块。
网络答案和备用答案都避开刚显示的文字，退出再进入从书封重新开始。
没有新增NVS格式、Flash内容缓存、模型服务、后台定时更新或服务器部署。
页码为当前打开期间的翻阅次数，最多9999后回到1，不表示答案池中的顺序。

## 生命周期和调用链

`launcher.enter_app → app_answers.enter → answers_enter` 创建书封、隐藏答案层和底部按钮。
`LV_EVENT_CLICKED` 或 `answers_tick → buttons_control_pressed → start_turn` 启动联网请求，准备备用并禁用操作。
`answers_fetch_begin → fetch_task → HTTPS → answers_data_parse` 在独立FreeRTOS任务中拉取并校验JSON；
后台只发布带generation的结果，`answers_tick → answers_fetch_poll → poll_answer` 才更新LVGL。
`launcher.app_tick_timer → answers_tick` 每20ms推进一次720ms动效：

| 状态 | 前360ms | 后360ms | 完成 |
| --- | --- | --- | --- |
| 首次翻开 | 书封绕中心横向收窄 | 隐藏书封，显示答案并淡入 | 记录答案索引/页码，恢复按钮 |
| 再翻一页 | 当前答案淡出并轻微上移 | 中点换文字，新答案淡入 | 更新索引/页码，恢复按钮 |

`visibility(false/true)` 重新记录时钟；锁屏/快捷面板遮挡期间既不推进动效也不接受点击。
恢复时从同一阶段继续，不把遮挡时间算进动画；tick差值使用无符号时间差并限幅。
进入/遮挡/退出调用`buttons_reset_control`清掉旧PWR事件；翻页期间的连按被消费后丢弃。
静止页不再绘制，也不轮询HTTP；网络任务只由翻页触发，没有常驻timer/IMU或预取任务。
`go_home → app_answers.exit → answers_fetch_cancel` 清空UI引用、作废generation；实际对象由启动器删除。
HTTP任务检测取消后由自身close/cleanup/free并退出，不在SDK I/O中强杀任务。

联网等待时动效停在翻页中点前，显示“联网获取中”，可随时返回或锁屏。
客户端I/O超时6s、读循环总期限8s、UI等待上限8.5s；单个worker尚未清理时拒绝新任务，避免叠加TLS内存。
完整200、最多4095B、完整Content-Length/chunked正文和合法UTF-8/JSON短标题才接受；
保留TLS证书验证，禁止自动跨地址重定向；无网、HTTP失败、异常内容和重复内容分开呈现备用状态。

## 字体与防线

`tools/gen_answer_font.swift` 从该App和答案库的字符串收集字形，使用本地CoreText导出
`main/font_answer.c`：32px、4bpp、309字形、102,065B字形位图，包含标准中英文标点。仅裁去已经透明的边界，不缩小字体。
`tools/gen_location_font.swift` 增加本App文案作为24px字形输入，本轮新增10个字形；
全部1450个beta.19已发布字形位图逐字节保持一致。

动态中文通过`font_answer_cjk_32`扩展到GB2312一级3755个常用字，同样32px，新增字形为2bpp RLE，
位图540,034B；现有字形/其他App不改清晰度。选择[官方lv_font_conv](https://github.com/lvgl/lv_font_conv)
1.5.3（MIT）生成，开启已有LVGL压缩字形解码器。全GB2312的32px实验超过4MiB余量，未带入固件；
不扩大已批准分区、不缩小已实现字号。网络标题先校验实际字体覆盖/132px高度；未覆盖字符/过长标题明确转备用。

复现：用`CTFontCopyAttribute(PingFangSC-Regular,kCTFontURLAttribute)`找到本地集合，
`tools/gen_answer_cjk.py --font-collection <PingFang.ttc> --converter <lv_font_conv-1.5.3>`导出C源；
工具需本地fontTools和官方CLI，不参与设备运行，不上传字体或项目代码，不打包TTF原件。
本轮临时工具按`npm install --prefix <temporary-dir> --ignore-scripts lv_font_conv@1.5.3`安装，交付后清理。

`main/launcher.c` 的App数量与 `main/launcher_icons.h` 的枚举编译期一致；
`tools/preview_launcher_icons.c` 同样校验名字表，预览覆盖新增书本图标及24px中英文名字。
`tests/answers/answers_ui_tests.c` 使用真实LVGL/App/字体，替换语言与硬件随机输入：
覆盖全部48×2短句字形、圆屏边界、首次/再次翻页、重复点击、不连续重复、遮挡恢复、
时钟回绕/延迟、页码界限、静止无重绘、退出中断和重新进入。
新增联网/备用/长内容/不支持字形/超时/实体键/遮挡旧按键的UI场景；
`answers_network_tests.c`运行真实fetch/parser，注入SDK边界故障并检查旧请求作废与HTTP资源释放。
网络回归另校验每次请求URL具有新的随机附加值；该断言在固定URL实现上失败、修订后通过。
测试截图中的网络内容/74%电量为夹具；不能将原生截图、主机构建或发布包当作设备操作验收。

通过host CMake的 `answers_book_and_lifecycle` 运行回归；给 `answers_ui_tests` 传已有目录可导出原生PPM预览。
有效构建、最终原生截图和发布/设备结果分别记录在 `PORTING_NOTES.md`。
