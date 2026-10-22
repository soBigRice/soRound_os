# 锁屏表盘：已发布30款，设备已安装44款

## 2026-10-22 设计八款（已部署及安装）

按用户要求继续增加视觉差异更大的设计，追加ID36–43：`mono / bauhaus / seven / vinyl / prism / metro / moon-gate / orrery`。旧0–35的名称、编号及绘制保持；仍为五组、同一NVS字段、同一刷新与选择控制器。用户已确认保留八款，并明确授权提交、官网部署及设备烧录；功能提交`3e7122e`已推送，官网44款部署与设备安装均已完成。本轮不创建公开固件Release，下载渠道仍为既有公开版本。正常/AOD、午夜、23:59及实际233px预览见[八款原生审阅](./artwork/watchfaces/atelier-native-20261022/README.md)。

`watchface_ui.c:watchface_render`保留前三段分发，STUDIO组内15–22交给`watchface_atelier.c:watchface_atelier_render(kind=studio-15)`。新模块只消费现有`watchface_data_t`，共用`watchface_studio_draw.h`、成对字号与黑色底面，不增加位图、字体、线程、计时器、服务或缓存。留白/构成为模拟指针，电子自绘七段数码管，唱片/棱镜/月门为数字钟，线路/星仪为上下排列的时分；各款秒标由真实秒数驱动，星仪的三颗轨道点分别编码时分秒。唱片、线路、月门、星仪是视觉意象，不代表音乐播放、交通、月相或天体数据。

AOD在装饰绘制前统一返回`wfs_aod`；同分钟不因秒变化重绘，下一分钟刷新。23款STUDIO仍仅在唯一`taskLVGL`线程使用原有共享scratch；本轮不改任务栈、分区及公开OTA渠道。

2026-10-22最终代码验证：5/5相关主机测试通过，覆盖44款目录/两个选择入口/保存、AOD、睡眠唤醒与资源访问；736组全帧/40行双缓冲结果一致，覆盖23款STUDIO、两种RGB565色序、466/233px、正常/AOD与四组边界时间。原36款102张正常/AOD/固定参考帧与修改前逐字节一致。ESP32-S3候选`v1.7-beta.37-atelier8`构建成功，镜像5,251,872B；这不代替新八款的真机栈、性能、功耗或主观验收。

官网目录44款，公开固件已有30款、未随公开固件发布14款；构建数量从目录派生。GitHub Pages工作流`39724945016`成功，中英文线上44款共88次选择、44/30/14筛选与Enter操作通过，1380/390/320px无横向溢出，无页面错误；83个公开文件与本地构建逐字节一致。网站的未发布标记指公开固件Release，不代表官网尚未部署。

设备：隔离验证保留真实绘制、两处选择器与9216B生产任务栈。八款全屏、独立App预览、设置预览各两轮（每阶段16次），八款AOD、跨分钟及休眠唤醒均完成；173.60秒无监测fault，最低LVGL可用栈3016B。随后安装不含probe符号的普通`v1.7-beta.37-atelier8`（5,251,872B，SHA256 `0ea82aecc1e9baf25038e3785572fc411df8282acd4377b55403c3f65849f20e`），70.06秒启动观察、自检和首页首帧通过。最终新鲜读回ota_1序号56/VALID、恢复槽序号55/VALID，两CRC有效；应用物理MD5匹配，原引导器、分区、beta.35整个恢复槽和storage整个文件区保持。NVS在本次完整备份、两次安装前后均逐字节一致。真实屏幕主观观感/触摸、帧率及功耗仍未据此宣称验收。

本次实际依赖为ESP-IDF6.0.1 / LVGL9.5.0；生产与probe的路径、header和依赖锁均已核对，锁文件与前六款安装归档相同。依赖版本不能仅继承历史文字。私有32MiB完整恢复备份、准确普通固件及ELF/config/依赖锁、小型原始回执保留在忽略目录`build/flash-records/atelier8-20261022/`；隔离probe构建、临时网页、串口、浏览器页及预览服务结束后清理。

构图回归要同时看10:08、00:00与23:59以及真实233px预览：日期区应先于连续指针绘制，避免截断秒针；唱片数字的标签留白必须容纳较宽的00:00，不能只用默认样例判断文字边界。此次已据实图修正并复核，正常图、边界图及验证摘要保存在上述审阅目录。

## 2026-10-22 图形六款与官网完整图鉴（已部署及安装）

用户要求增加表盘设计，并在官网展示已有表盘。追加STUDIO ID30–35：`sector / solar / grid / petal / ribbon / binary`，既有0–29编号、名称和绘制不变；仍为五组，不扩展NVS格式、分区、计时器或数据服务。用户已确认设计并授权提交、官网部署及设备烧录，功能提交`83ba596`已推送；官网与设备安装现已完成。本轮不创建公开固件Release，不能用下方beta.37历史设备记录证明新款真机效果。

`watchface_ui.c:watchface_render`按STUDIO组内序号分发：0–2→`watchface_instrument_render`，3–8→`watchface_scenes_render`，9–14→`watchface_graphic.c:watchface_graphic_render`。六款读取同一`watchface_data_t`时间快照，正屏466px与预览233px共用现有原语；扇区弧、日轮标记、网格扫描、花瓣秒点、色带刻度和二进制行都由真实秒数驱动。日轮不提供真实日出/日落或天气信息，二进制按左高右低的32/16/8/4/2/1权值显示24小时、分钟和秒，并保留十进制读数。

新款的AOD在装饰绘制前返回`wfs_aod`，保持同分钟静止、下一分钟刷新；隐藏/熄屏取消计时器，唤醒补画的原控制器逻辑保持。无新线程、常驻全屏画布、网络请求或自定义素材写入。新款数字仅用已有成对字库；不可把只作为72px半尺寸存在的36px字体当作一对36/18px使用，未定义的18px会落回14px。`studio_font_contract`显式检查新款30/15、96/48、144/72字体匹配，避免仅用渲染hash掩盖比例变化。

官网单一目录为`site/watchfaces.json`。`scripts/build_site.py:watchface_gallery`逐项核对固件`watchface_catalog.c:names`，生成无需JavaScript也能看到的36款缩略图；大图、样式按钮、筛选和中英翻译使用同一目录。六款新设计显式标记未发布，STUDIO九款图片同步当前本地黑底实现。网站预览不读取或连接设备。维护与导出入口见[官网说明](../site/README.md)。

验证：5项相关主机回归通过，覆盖36款目录/选择/保存、AOD、隐藏和休眠唤醒；STUDIO共480组全帧/40行双缓冲比较在RGB565和RGB565_SWAPPED、466/233px、四组边界时刻下相同。修改前后旧表盘78张正常/AOD/固定参考帧逐字节一致。ESP32-S3构建通过；后续设备证据见下段，未测主观触摸/观感、帧率或功耗。中英文网站各36款点击（72次）及1380/390/320px无横向溢出检查通过，无页面错误；筛选与键盘Enter通过。[六款原生审阅](./artwork/watchfaces/graphic-native-20261022/README.md)保留正常/AOD图与回执。设计图与官网效果已由用户确认；真实屏幕的主观观感/触摸仍待用户体验。

部署及安装：GitHub Pages工作流`39722087908`成功，线上中英文交互及75个公开文件逐字节核对通过。隔离驱动保持生产绘制与9216B任务栈，六款全屏、独立App预览、设置预览各两轮，六款AOD、跨分钟与休眠唤醒全部完成，174.14秒无监测fault，最低栈余量3064B。随后安装不含测试符号的`v1.7-beta.37-faces6`（5,235,472B，SHA256 `cd6fd9d0ed29fcb9e1e151ba170136c7125774ffae6ed1ce46f8a67aba4d5b98`），普通固件70.09秒自检/首页通过。新鲜读回ota_1序号52/VALID、恢复槽序号51/VALID，两CRC有效；镜像校验通过，引导器、分区、beta.35整个恢复槽与storage整个文件区保持。NVS与最新安装前快照逐字节相同，保留ID21。没有创建公开固件Release或改公网OTA。

NVS基线经验：完整备份时face为28，但首次测试写入前已为21；官方ESP-IDF解析器确认差异仅为`settings/face`，轮测前后NVS逐字节一致。最终写入曾被“必须与更早完整备份一致”的保护拦住，未执行该次写入；核对三个时间点后保留最新已验证状态继续。运行中的设置可变化，备份、安装前、测试后的快照必须分开比较，不能把旧备份当作永恒状态，也不能为通过校验覆盖后来的选择。详细原始证据保留在忽略目录`build/flash-records/watchfaces6-20261022/`；原始备份、最终精确镜像/ELF保留，隔离构建和临时进程清理。

> **2026-10-20发布状态**：[beta.37](https://github.com/soBigRice/soRound_os/releases/tag/v1.7-beta.37)已发布，恢复STUDIO九款并保留原21款；beta.36继续撤回。9216B配置的probe3已完成九款全屏/预览/AOD与生命周期轮测，最低可用栈2952B。精确CI发布包USB安装通过，普通生产固件70秒观察无fault，启动选择新鲜读回VALID，原数据及beta.35恢复槽校验保持。恢复与验证见[事故记录](./PORTING_NOTES.md#2026-10-20-beta36撤回与旧槽恢复)及[beta.37组合记录](./PORTING_NOTES.md#2026-10-20-beta37组合修复与验证)。

2026-10-20：用户确认[九款设计](./artwork/watchfaces/exploration-20261020/README.md)全部接入，最初要求高还原度、保留旧款、测试发版且不烧录；事故恢复后另行授权应用烧录。STUDIO追加为ID21–29，总数30；原0–20保持名称、映射、字库及绘制逻辑。见[原生对照](./artwork/watchfaces/studio-native/README.md)，发布状态另见[发布记录](./PORTING_NOTES.md)。历史21款设备/用户证据不涵盖新款。

## 独立表盘 App（2026-10-20，本地候选）

启动器末尾追加第24个入口“表盘 / Watchfaces”，前23项顺序保持。设置 → 表盘保留为快捷入口；两处共同使用`watchface_picker.c`，不维护两套选择规则。当前改动尚未发布；已按用户明确授权USB安装`v1.7-beta.37-watchfaces`，通过启动自检、70秒观察和镜像/数据完整性校验，真实观感仍待用户验收。

`app_watchfaces.c:watchfaces_enter → watchface_picker_mount → picker_rebuild`直接使用调用者的现有面板，保持233px预览、中心(232.5,232.5)、左右循环、五组按钮和即选即存；不增加绘制容器深度。独立App使用系统返回键/右滑回启动器；设置入口继续返回原设置列表并恢复滚动位置。

`picker_face / picker_theme → watchface_select → settings_set_face → settings_save → picker_queue`先保存，再异步重建子控件，避免在按钮事件中删除事件目标。每个入口各自持有`watchface_picker_t`，实例随专用panel识别。`watchface_picker_unmount`及面板删除回调均取消待执行重建、清空预览指针；退出后tick无操作。两个入口沿用App框架每秒刷新，锁屏/覆盖时暂停，没有新增计时器、线程、存储键或分区迁移。

验证入口为`settings_controls_and_level_recovery`及其设备色序版本、`launcher_bilingual_app_name_glyphs`：覆盖两语言、30款循环、分组、保存/重进、排队重建时退出/直接删页，以及真实启动器进入/锁屏/解锁/返回。原生对照和验证状态见[独立入口检查](./artwork/watchfaces/app-native-20261020/README.md)。原设置页及原水平仪基线逐帧校验，防止抽取控件时引入布局或共享夹具污染。

## STUDIO 九款与高还原绘制

| NVS ID | 名称 | 活动态刷新 |
| --- | --- | --- |
| 21 / 22 / 23 | radar / tape / iris | 秒扫线 / 卷盘 / 秒标，1 Hz |
| 24 / 25 / 26 | maze / fold / pixel | 迷宫秒球1 Hz / 翻页分钟变化 / 像素场景1 Hz |
| 27 / 28 / 29 | horizon / tide / growth | 地平线分钟变化 / 波纹浮标1 Hz / 时分秒针1 Hz |

`watchface_ui.c:watchface_render`先识别STUDIO边界，前三款交给`watchface_instrument_render`，其余交给`watchface_scenes_render`；之后才走HAND和原三组。不能把所有15以后的ID都当作HAND。

`watchface_studio_draw.h`只服务新组，不修改旧21款绘制。它按原稿基线、字重、字距、图层和透明度绘制；对细线、圆与多边形保留亚像素覆盖率。原SVG圆弧描边以路径居中，不能直接套LVGL向内描边及整数角度，否则迷宫半径与缺口位置会错。扫描仅覆盖当前DMA分块可见行；beta.37由`watchface_studio_draw.c`提供唯一2084B BSS工作区，存放`px/py/xs[96]`和`coverage[466]`，不再把扫描数组放入深层绘制栈，不增加全屏常驻画布、SVG引擎或独立计时器。

工作区只在唯一`taskLVGL`线程内供非重入STUDIO绘制复用。当前`LV_USE_OS=0`，矩形描述符不绑定触发STUDIO回调的对象；同步软件后端不会重入这些helper，且已创建的draw task复制描述符、不引用扫描数组。后续若改变绘制线程或回调链，必须重新核对这个契约，不能无条件跨线程复用。

原稿小字8–15px、精确数字字号及233px预览分别由`gen_watchface_fonts.py --studio`生成；Barlow来源/OFL、4bpp无损存储沿用旧方案，旧字体文件不重新生成。文字保留1/16px原始字宽与小数字距，并按SVG语义折叠连续空格。字库回归检查正屏和预览的数字、冒号、小标签及中点，要求`resolved_font`就是目标字体，避免fallback掩盖缺字。

旧`settings/face`仍为uint8，无NVS格式或分区迁移。设置保留233px预览中心(232.5,232.5)、左右循环、即选即存和返回语义；五组按钮在圆边安全范围重新居中。进入STUDIO默认radar，同组点击保留当前款；旧三组之间仍保留五种用途，HAND/STUDIO切回旧组从dots开始。

新款AOD在装饰前返回静态核心时间，并对齐下一分钟；隐藏/熄屏仍删除计时器，唤醒补画。HORIZON与FOLD本来不依赖秒，不应为满足泛化测试而增加原稿没有的动画。新款不请求天气、图片、心率、GPS、麦克风或音乐桥接；地平线、潮汐、像素场景只是时钟意象。

启动经验：`startup.c`原先按`face%5==4`等待图片，扩展后会把HAND19、STUDIO24/29误算为主题图片。现在限定`face<WATCHFACE_LEGACY_COUNT`，4/9/14的真实解码等待保持；启动单测覆盖19/24/29不触发图片工作，真实全流程覆盖新首末ID恢复。

验证入口：`watchface_collection_and_power`覆盖30款、旧ID、精确字形、秒/分钟变化、AOD、休眠与资源访问；`settings_controls_and_level_recovery`覆盖双语布局、20→21、29↔0、保存与返回；`startup_studio_first_flow/last_flow`覆盖启动恢复。`tools/render_studio_review.py`以2026-10-20 10:08:36导出正常/AOD与设置页，对照批准SVG并保留RGB565比较回执。截图检查不等于真机亮度、功耗或主观验收，不能宣称浏览器与AMOLED每个像素完全相同。

## beta.36事故与beta.37验证

2026-10-20现场备份确认崩溃时`settings/face=21`，实际为RADAR。准确发布ELF解码显示任务控制结构已损坏；完整对象递归、同步软件绘制以及协处理器/中断保存区叠加，使7168B任务栈存在超容量风险。未捕获首次越界写点，不能把单个helper的栈帧或Wi-Fi/NVS正常读取链当作最终根因。初次主机截图和50项通过漏掉了真实任务栈约束。

共享scratch候选保持采样、颜色、透明度、路径与刷新率不变，不关闭看门狗。最初的单变量probe沿用7168B任务栈：发布依赖LVGL9.6.0~1的实际ELF中，两个渲染编译单元的`wfs_poly_opa`均从2368B降至288B，`wfs_ring_opa`均从1232B降至288B；全镜像只有一份2084B `g_wfs_draw_scratch`。静态回执为私有`build/flash-records/beta37-validation-20261020/studio-target-stack.json`，它记录当时的7168B本地probe，不是最终发布镜像。

独立重编原beta.36 header和候选header，冻结其余测试对象及LVGL库，在相同显示格式内比较：48行RGB565各89/89帧逐字节一致；40行双缓冲RGB565_SWAPPED也各89/89帧一致，涵盖旧21款、新9款的正常/AOD及固定日期参考。两种格式间既有像素差异不纳入本次scratch回归；这项结果证明候选前后像素等价，不证明原事故已在真机解决。

用户后续授权应用烧录。probe1安装选择为seq38/ota_1 NEW(0)，运行后新鲜读取已为VALID(2)，另一恢复槽仍为VALID且CRC有效。观察器重连后的`startup OTA action=0`属于再次启动，不能据此推断引导器没有确认能力。该实证只覆盖健康启动确认；本轮未做失败自动回滚测试，也未改写引导器或迁移分区。

probe2在7168B栈下完成287.6秒轮测：九款全屏、设置预览20→21–29→0→29→21、RADAR连续70秒、AOD跨分钟与木鱼画面阶段均未见fault；全屏阶段最低可用栈1000B，进入实际设置层级233px预览后历史最低降到424B。424B余量不足，不能把这轮无panic写成最终生产验收。

据此beta.37在`display.c`显式设置`port_cfg.task_stack=9216`，比7168B增加2048B内部RAM；scratch保持不变。这是独立的生产余量保护，不以加栈反向证明原事故首次坏写原因，也不降低画质或频率。优先级5、core1亲和性和看门狗保持；9216B下的实际余量由后续probe3重新采集，不使用424+2048代替实测。

probe3已通过真实launcher `enter_app/go_home`链、39次设置预览、九款全屏/AOD、跨分钟、sleep/wake与RADAR70秒，约283秒到达DONE，无监测fault。9216B配置下实际最低可用栈2952B；开机内部RAM最低73451B。独立看门狗从`taskLVGL`句柄采样，IDF单位为字节，避免在绘制回调打印。发布CI已成功，精确发布ELF再次确认四个helper均288B、唯一2084B BSS及9216B任务栈，且无probe驱动符号。精确公开包USB安装后普通生产固件观察70.0955秒无fault，新鲜元数据为seq44/ota_1 VALID(2)、seq43/ota_0 VALID(2)，两CRC有效；启动可用栈3256B与内部RAM最低73435B属于该启动观察，不替代probe3完整轮测最低值。分区表、引导器、原数据及beta.35恢复槽校验保持，见[组合验证记录](./PORTING_NOTES.md#2026-10-20-beta37组合修复与验证)。本轮未测网络OTA或失败自动回滚；触摸、主观观感与扬声器仍须用户验收。

下文为既有21款的历史实现及验证证据。

2026-10-08：当前工作区在旧十五款后追加 HAND 六款，索引15–20；用户已授权按设计开发。
原生466×466绘制、设置选择、逐秒刷新、常显和旧图片缓存回归通过，本地ESP32-S3构建通过。
用户已要求发布`v1.7-beta.32`内测版本，发布证据见[发布记录](./PORTING_NOTES.md#2026-10-08-v17-beta32-指针表盘流体双模式与固定迷宫关卡)。
新增六款尚未烧录或通过用户/设备观感验收；此前十五款的设备证据不覆盖新增六款。
[HAND原生图](./artwork/analog-play/native/hands.png)、[选择页](./artwork/analog-play/native/selector.png)；
流体双模式与固定迷宫关卡见[小游戏实现](./PLAY_UI.md)。

核对日期：2026-10-05。用户选择保留三个视觉方向，已实现固件与原生主机预览；
首次十五款实现已随本地 OTA 页面修复固件通过 USB 写入设备并正常启动；
实现已提交并随 `v1.7-beta.24` 内测包发布；设备已通过 OTA 从本地 beta.23 修复候选升级、
校验和重启，随后更新检查成功。主观效果、真机触摸/亮度/功耗尚待验收。
此前本轮无损字体压缩随获批的 TLS PSRAM 固件通过 USB 写入当前应用分区并正常启动，
该 USB 写入的分区表与 OTA 元数据前后完全一致；本次 OTA 则按正常流程切换启动槽。

2026-10-05 beta.24 用户指出图片仍是旧熊猫、部分排版未还原及重复感。
2026-10-06 用户确认发布本轮修正，已发布内测版本`v1.7-beta.25`；本轮尚未写入设备。
真实 LVGL 原生预览见文末，不将旧 beta.24 的启动结果当作本轮验收。

## 选择与兼容边界

| 组别 | NVS 索引 | 表盘 |
| --- | --- | --- |
| TYPE | 0–4 | dots / bold / rings / weather / image |
| ORBIT | 5–9 | dots / bold / rings / weather / image |
| SHIFT | 10–14 | dots / bold / rings / weather / image |
| HAND | 15–20 | mark / arc / numeral / orbit / frame / dots |
| STUDIO | 21–29 | radar / tape / iris / maze / fold / pixel / horizon / tide / growth |

`settings.c`原有`uint8_t face`及NVS键保持；索引0–4继续对应原来的五种用途。
设置 → 表盘：左右循环全部二十一款，底部四个组名可直接跳组。
旧三组之间跳转保留用途；首次进入HAND选择mark，离开HAND进入旧组的dots，同组点击保留当前款。
`watchface_catalog.c`集中管理名称与分组边界；HAND不套用旧`/5`与`%5`映射。
选择立即生效、保存；不在表盘上增加手势，也不改变BOOT短按锁/解锁、长按关机和PWR计时控制。

`watchface_name/theme_name/kind_name`是选择页的名称入口。缩略图为233px真实绘制，
不使用天气图标或假“12:34”代替表盘；设置页已有App计时器每秒调用`watchface_refresh_preview`，
缩略图没有独立计时器，重建/退出由父面板销毁。
2026-10-07用户要求表盘居中、设置项独立进入：预览固定在(116,116)，233px预览中心与466圆屏中心一致，
左右选择和底部主题组完整可见，不参与纵向滚动；直接从设置首页进入，返回恢复首页位置。
十五款渲染、索引与保存语义保持；[当前原生设置稿](./artwork/settings-detail/README.md)已本地验证并随beta.30发布，真实设备与观感待验收。

## 调用链与状态

```text
lock.c:lock_set → watchface_show/hide
  → watchface.c:snapshot → 同一时间 / Wi-Fi / power_read / weather_cached / img_store 缓存
  → watchface_ui.c:watchface_render → 主题与用途组合 → 原生LVGL绘制

app_settings.c:rebuild(SETTINGS_FACE) / app_watchfaces.c:watchfaces_enter
  → watchface_picker_mount → picker_face / picker_theme
  → watchface_select → settings_set_face → settings_save → picker_queue
  → picker_rebuild → watchface_create_preview → 共用watchface_render
```

每个表盘只有一个绘制对象，没有为每个数字/刻度创建大量控件。
日期、SSID、IP、电量、温湿度及高低温均来自运行状态，预览中的主机样例不能当设备遥测。
SSID最多32字节，中心区域允许换行；Wi-Fi图标与长SSID错开。电量读取失败显示`--%`，
断网清空IP；天气失败明确显示不可用，温度不会冒充0℃。
2026-10-07起网络状态采用 `wifi_service_ready`，AP已关联但DHCP/IP未就绪时不显示已联网；
原分钟/强制刷新时机保持，天气缓存共用同一后台请求。详见[联网恢复逻辑](./NETWORKING.md)。
天气复核包含高低温独立变化，修正了原表盘只比较温度/湿度/天气码而遗漏范围变化的问题。

| 状态 | 刷新 / 资源行为 |
| --- | --- |
| 活动态 | 旧点阵款和HAND六款每秒更新；其他旧款按分钟或缓存变化重绘 |
| AOD | 旧秒点停止、图片背景隐藏；HAND隐藏秒显示及附加信息，保留刻度/时分针；对齐下一分钟 |
| 熄屏 | 删除表盘计时器，保留遮挡底层触摸的根对象 |
| 唤醒 | 恢复计时器、强制读取并补画当前时间 |
| 隐藏 | 删除表盘计时器；不在后台继续绘制 |

亮度和充放电熄屏判断继续由`lock.c`控制；未据主机渲染宣称功耗提升。

HAND由`watchface_ui.c:hand_face`绘制：mark完整刻度/四数字；arc分段外圈/镂空时针；
numeral十二数字/柳叶针/小秒盘；orbit圆形时标；frame方形轨道/平头针；dots点阵刻度与真实日号。
时分针角度含秒/分进度，日期和电量来自同一`snapshot`，不触发天气与图片加载。
新增Barlow数字子集12/24 Regular、16/32 SemiBold由`gen_watchface_fonts.py --hands`生成，
沿用原OFL来源、4bpp无损编码，旧`font_watchface.c`及字形像素保持。

原生视觉复核发现：LVGL独立三角形各自抗锯齿，拼接会在实心针内部留缝。
`needle_polygon`改为凸轮廓逐行填充，仅外缘做四次采样覆盖，避免增加矢量引擎或图片资源。
`play_tests:native_hands`检查时针内部整段保持实心；不可只用每款hash不同判定视觉正确。

## 字体、图像与取舍

采用原生LVGL绘制及既有天气图片，不增加SVG引擎或UI依赖。
`watchface_ui.c`只为锁屏维护新的圆点数字，`glyph.c`中其他计时App字形保持。
实体数字使用OFL许可Barlow，4bpp字形只导出数字/标点和小号文字；原中文fallback沿用`font_cn16`。
本轮按设计稿区分字重与比例：堆叠大数字188px Black、横排时间104/112px SemiBold、
环形时间96px Regular、天气温度56px SemiBold；均有对应半尺寸字形供233px缩略图使用。
SemiBold同样取自下方固定commit，新增字体文件SHA256已纳入`manifest.json`和素材校验。
2026-10-05 为恢复 OTA 槽空间，在原 Pillow 像素上采用 LVGL 原生无损 RLE/XOR 存储；
压缩无收益的7/9px字体保留原存储。字体生成需要 Python/Pillow 与 Node，使用
`tools/vendor/lv_font_conv` 固定 commit 的官方 MIT 编码器，没有 npm 或固件运行时依赖。
`watchface_tests:font_bitmap_digests`核对13字号全部501字形的像素和度量；
压缩前后38张466×466原生帧逐字节一致，设置缩略图回归通过。实际设备的刷新观感仍待验证。
没有导出整套大字库、降低字形灰阶或把整张表盘烧成图片。

2026-10-05核查[Barlow官方来源](https://github.com/google/fonts/tree/6cdf01867df0813c2390f90dff7dc66c87f14cf7/ofl/barlow)
及[OFL许可](https://github.com/google/fonts/blob/6cdf01867df0813c2390f90dff7dc66c87f14cf7/ofl/barlow/OFL.txt)；
原字体、许可证、commit和SHA256位于`artwork/watchfaces/fonts`。
完整许可也通过字形对象`user_data`保留在固件中。
没有沿用系统字体作为新增大字库输入，以便随固件分发来源明确。

三张默认背景由内置Image Gen依据用户选择的三张设计稿生成；
生成约束为466×466方形、无文字/钟面、顶部留黑、分别为雾中山峦/月下山峦/斜向岩石山体。
使用既有ImageMagick转换为466×466基线JPEG、4:2:0、quality=90，不改变构图有效内容。
最终文件为`artwork/watchfaces/type.jpg / orbit.jpg / shift.jpg`，设计目标保存在`reference/`。
`gen_watchface_backgrounds.py`嵌入JPEG，因此OTA也携带默认图片，无需另刷storage分区。

`img_store_face_image_for(theme)`先异步读取原`bg.jpg`，以旧出厂熊猫的长度22494与
CRC32 `8734e6d4`识别该静态素材，再决定是否解码。CRC调用ESP-IDF 6.0.1的
[ROM CRC API](https://github.com/espressif/esp-idf/blob/v6.0.1/components/esp_rom/include/esp_rom_crc.h)，
只用于旧默认素材识别，不作为可信性/安全校验。出厂熊猫不生成RGB565缓存，直接启用所选主题背景；
真正自定义图片仍优先，包括相同文件名、同样大小但内容不同的图片。
缺失/解码失败也使用主题默认图。每主题最多一个解码任务与一份RGB565缓存，
三个默认缓存的上限为1,302,936字节PSRAM；任务结束自行退出，没有图像重复解码循环。
任务/内存/解码/尺寸失败结束加载并显示失败文字；默认图严格验证466×466。
图像只作适度统一压暗；时间/日期区域较亮时提高压暗比例保证可读，不覆盖矩形黑卡。
AOD隐藏图片；原自定义`bg.jpg`文件不会被覆盖。

## 绘制经验与回归

2026-10-21追加授权：九款STUDIO正常/AOD背景改为纯黑，保留全部对象、logo/文字、坐标和动效；仅将MAZE深色墨迹改为浅绿/米白以适配黑底，月牙镂空和IRIS内孔随底色改黑。旧21款不改。[黑底原生审阅](./artwork/watchfaces/black-native-20261021/README.md)含九款及常显；这是主机实际LVGL输出，不是真机照片。

第二阶段绘制优化：`wfs_span`用额外40B BSS中的四槽合并同一原语内相邻等色行；原语结束前全部提交，不跨原语改变叠层。`wfs_geometry_cache()`仅在IRIS/GROWTH使用原函数计算并复用不随时间变化的浮点路径，精度不改；13,924B显式PSRAM缓存同时适配466/233px，不是全屏位图。`watchface_select`、`watchface_hide`与`watchface_picker_unmount`释放缓存，分配失败沿用原计算路径，同一生命周期不反复申请。

优化后的2160帧与未优化的黑底参考逐像素一致；禁用缓存的2160帧也相同。比较夹具显式设置与真实`surface()`相同的黑底，避免未设样式对象的默认背景污染结果。两种RGB565格式的整帧/40行双缓冲和窄区域恢复继续通过。相对原生产绘制器的黑底参考，九款全屏每帧任务减少约10%–47%（RADAR 34.61%、IRIS 12.56%、GROWTH 10.23%）；这是主机相同后端的确定性任务计数，不能换算成ESP32帧率。旧21款的任务/推送/混合计数不变。

最终目标ELF仍只有2084B扫描工作区和40B行合并槽；poly/ring各304B、label368B、growth320B，LVGL栈配置仍9216B。后台保存worker另分配3072B任务栈，未以主机或静态帧大小冒充真机最低余量。完整59项主机与2项生态箱/专注检查、发布依赖ESP32-S3构建通过；本轮尚未烧录或发布，当前私有候选及摘要见[追加验证记录](./PORTING_NOTES.md#2026-10-21-全量流畅性优化与黑底表盘本地候选)。

2026-10-21性能修正：`watchface_studio_draw.h::wfs_poly_opa/wfs_ring_opa`将覆盖率清零、填充和扫描限定为图形边界与当前块相交的列，保留原四次采样、覆盖率/透明度舍入及圆屏裁剪。`wfs_label`按真实字形墨迹边界跳过块外字符，保留原1/16px字宽、空格规则、字距和基线；没有增加全帧缓存、降低分辨率或减少秒动效。旧21款绘制器保持原样。

新增`studio_full_frame_and_dma_tiles`使用真实controller/surface/render链，在两种RGB565格式、466/233px、正常/AOD及四组边界时间下比较288组整帧/40行双缓冲像素，再逐组清空并恢复窄区域，核对横向及非整块裁剪。独立冻结的修改前程序与最终程序还完成2160帧逐像素对照，全部一致。全套56项主机回归通过；没有以视觉正确性替代设备性能结论。

ESP-IDF6.0.1/LVGL9.6.0~1最终ELF：两组poly/ring栈帧各304B（原288B），label各368B（原320B），唯一共享BSS仍2084B；配置实码仍为9216B。无大数组回栈或新增图形堆缓冲。这些静态数字不能替代完整层级的真机栈最低余量；本轮未烧录。主机重负载表盘时间下降，但不同批次有明显波动，不报告真机FPS或产品提升百分比。见[本地验证记录](./PORTING_NOTES.md#2026-10-21-流畅度修复本地未发版未烧录)。

此次遗漏：原实现把所有可解码的`/img/bg.jpg`视为自定义图，未区分随旧storage分区出厂的熊猫。
OTA保留storage，因此设备必然走旧图分支；此前主机预览直接注入三张主题图，绕开了这一真实入口。
防线改为用仓库实际`images/bg.jpg`做缓存控制器回归：修正前断言失败，修正后分别返回三组默认缓存；
同长度改单字节的图片及其他自定义图仍作为用户覆盖。只读挂载、不改文件、NVS和分区表。

设计复核必须对照`reference/`和同一`watchface_render`输出，不能以十五张hash不同或字形无损代替还原验收。
本轮修正TYPE点阵底部横向信息栏、亮刻度/小时点与环形大时间；ORBIT六段侧弧、矩形分钟刻度、
小时分段/圆形当前位置及红色冒号；SHIFT左对齐日期/图片时间、错位大数字、粗进度弧与剩余点轨/图例。
天气低高温恢复原稿箭头和分隔线，SHIFT保留红色分隔。长SSID仍按真实数据换行，三组都有32字节样例。
小时/分钟标记按真实时间计算；设计稿中示意标记位置不作为写死角度的依据。

LVGL的圆弧角度字段为无符号值，钟面以顶部为零点的`-90°`必须归一化后提交。
直接传负角度会把少量分钟进度错误地画为大段圆弧；已用10:08的六点位置亮度检查复现旧画法失败、修正后通过。
宽小时分段使用平端，避免圆端填满相邻间隙。
依据[LVGL9.5原生绘制API](https://lvgl.io/docs/open/9.5/main-modules/draw/draw_api)
及本机9.5头文件核对；支持现有9.5与9.6兼容封装。

`tests/watchfaces/watchface_tests.c`：二十一款可区分、HAND逐秒刷新且不访问图片/天气、AOD分钟对齐/秒内稳定、睡眠与跨小时唤醒、
索引边界、时分进度语义、高低温独立更新、无天气/负温、32字节SSID、低电量/读取失败、图片加载及亮色自定义图。
`image_store_tests.c`：真实出厂熊猫识别、同长度不同内容的自定义优先、每组只起一次任务、完成清理及任务/分配/解码/尺寸失败；
这里的codec是夹具，不冒充开发板ROM JPEG验证。
`settings_level_tests`：二十一种缩略图、中英文圆形边界、HAND跳组、14→15和20↔0循环、保存/逐级返回和既有设置/水平仪回归。

实际JPEG格式/尺寸和字体SHA由`tests/watchfaces/check_assets.py`验证；
原生视觉复核见[TYPE](./artwork/watchfaces/native/type.png)、[ORBIT](./artwork/watchfaces/native/orbit.png)、
[SHIFT](./artwork/watchfaces/native/shift.png)、[选择页](./artwork/watchfaces/native/selector.png)、
[常显](./artwork/watchfaces/native/aod.png)。布局已查看；字体使用Barlow而非设计稿生成器的不可追溯字体，
照片为单独生成的同方向素材，非设计稿截图裁切。主观体验待用户验收。
本轮LVGL 9.5/9.6各三项主机回归（表盘/背景缓存/设置预览）及素材校验通过；目标ESP32-S3固件构建通过。
本地ESP-IDF 6.0.1 / LVGL 9.5候选大小4,053,696字节，4MiB槽余140,608字节；
正式CI使用LVGL 9.6，发布时仍须以对应CI二进制的槽检查为准。TLS PSRAM配置及编译保护保持。
beta.25实际CI使用LVGL 9.6.0~1 / esp_lvgl_port 2.9.0，发布包4,087,360字节，槽余106,944字节。
GitHub/R2/国内OTA全文摘要相同，国内条件Range验证通过；完整版本/摘要及通道状态见
[发布记录](./PORTING_NOTES.md#2026-10-06-v17-beta25-表盘纠正发布)。
当前未检测到USB设备，本轮真机显示尚未验证；beta.24的OTA稳定性验证不自动涵盖这轮视觉修改。

重建和验证：

```sh
python3 GeekTool-IDF/tools/gen_watchface_fonts.py
python3 GeekTool-IDF/tools/gen_watchface_backgrounds.py
cmake -S GeekTool-IDF/tests/host -B /tmp/soround-host
cmake --build /tmp/soround-host --target watchface_tests watchface_image_tests settings_level_tests buttons_tests -j 6
ctest --test-dir /tmp/soround-host -R 'watchface_|settings_controls_and_level_recovery|physical_button_mapping' --output-on-failure
python3 GeekTool-IDF/tests/watchfaces/check_assets.py
python3 GeekTool-IDF/tools/render_watchface_review.py --faces /tmp/soround-host/watchface_tests --settings /tmp/soround-host/settings_level_tests
idf.py -C GeekTool-IDF build
```

预览脚本在临时目录生成raw/PPM，完成即删除；保存的PNG是审阅产物。
下列大小与摘要属于此前十五款修正的构建，不代表2026-10-08新增HAND的镜像；当前构建证据见[小游戏实现](./PLAY_UI.md)。
此前固件构建通过：4,142,160字节，4MiB槽余52,144字节（约50.9KiB），未调整分区。
SHA256：`0248fa8b6a05ca4ea723e2da63080d5efdb014f6446f54e823a2b0847e36b404`。
四组目标回归、真实素材格式/哈希检查及最终Diff检查通过；未以此替代设备ROM解码或真机验收。
真机验收最小范围：选遍三组五款→检查圆边/图片加载与自定义图→空闲进入AOD→触摸唤醒→BOOT解锁。
特别核对Wi-Fi SSID、高低温、图片解码和常显亮度；尚未据自动测试标记为用户验收。
