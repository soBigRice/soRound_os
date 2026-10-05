# 设置、Wi-Fi与天气地址交互

核对日期：2026-10-05。设置与Wi-Fi随beta.19发布至GitHub/R2/国内OTA；天气地址功能沿用已发布实现。
镜像4MiB上限修正已部署，设备升级/触摸/无线操作仍待验收；[当前状态](./PORTING_NOTES.md#2026-10-05-v17-beta19-发布与镜像上限核对)。
本轮原生双语检查、固件及发布状态见 `PORTING_NOTES.md`；真实设备操作仍需单独验收。
浏览器旧样稿位于 [artwork/settings-location/index.html](./artwork/settings-location/index.html)，
其中地址/天气仍为样例；固件没有使用该样例列表。

## 设置页（本轮重做）

用户反馈beta.18设置交互不易使用；旧“每屏一项、上下循环七项”已被分类入口替代。
`app_settings.c:settings_enter → rebuild` 首屏显示四个可直接点选的卡片：显示与表盘、声音、语言、关于设备。
卡片宽度随圆屏上下收窄；字名24px、辅助信息18px、返回按钮44px。
不隐藏常用选项，不依赖用户记住上下页顺序。

- 显示：亮度读数/滑块、表盘入口和常显整行开关。亮度仍为64～255，拖动实时应用，松手才保存。
- 声音：音量0～100、静音整行开关及试听。沿用铃声/提示音的全局静音语义；离开声音子页即释放音频。
- 表盘：TYPE/ORBIT/SHIFT各含`dots/bold/rings/weather/image`五款，原索引0–4保持用途对应；
  233px真实缩略图、左右选择十五款、点击组名保留用途跳组，立即保存。详见[锁屏表盘](./WATCHFACES_UI.md)。
- 语言立即应用并重建本页；关于读取真实`esp_app_get_description()->version`，长版本允许换行。
- `settings_back`：表盘退到显示，其他子页退首页；首页再返回才由启动器退出。右滑沿用`app_t.back`调度。
- `queue_rebuild`延后销毁输入对象，退出取消待执行回调；开关可点整行或实际switch，事件不重复保存。

`control_ui.c/h`只供设置与Wi-Fi使用，统一卡片、单行文字、滑块和整行开关。
`launcher.c:header_app_style`为这两个App采用24px紧凑标题/44px返回按钮，天气原有40px返回样式保持。
`settings_level_tests`检查四分类入口、逐级返回、保存时机、整行开关、十五种表盘/跳组/循环、双语字形/圆形安全范围和音频释放。

## Wi-Fi页（本轮重做）

`app_wifi.c:wifi_enter`建立开关行、当前连接卡、扫描状态/刷新和独立滚动列表。
当前SSID与连接/认证失败/超时状态明确显示；扫描结果去重、保留信号和已保存/开放/需要密码标记。
已连接项只在上方卡片呈现，连接改变时`queue_rows → render_rows`用最近扫描快照同步列表，
不重新访问已释放的SDK扫描列表，也不在点击事件中直接删除当前行。
最多显示20条扫描记录；沿用SDK按RSSI降序的结果，忽略没有可选SSID的隐藏广播，不新增手动隐藏SSID功能。

点选开放网络直接连接；已保存网络用SDK现有配置重连；新加密网络打开独立密码页。
认证失败卡片可打开同一密码页，匹配当前保存目标时填入已记住的密码，默认掩码，可直接重试或修改。
取消只关闭输入页，不写配置、不发起连接。后退/空白处右滑关闭密码页，键盘/文本框不冒泡手势，防止输入时误退出。

密码页采用原生`lv_keyboard_set_map`：大小写、数字/符号和额外符号页，覆盖全部可打印ASCII；
明确顶部对齐，328×160键盘及连接/返回按钮均在466圆屏内。密码按UTF-8字节长度校验，
WPA/WPA2保留8～63字节及64位十六进制PSK，SSID32字节与PSK64字节不截断；WEP沿用对应长度入口。
没有手机配网、二维码或新网络协议，也没有替换NVS格式或擦除凭据。

`wifi_evt`只写服务状态/断开原因，`wifi_tick`在LVGL任务更新页面；
在调用`esp_wifi_connect`前清除旧尝试标志，避免同步/快速GOT_IP被随后清掉。
仍保留启动自动重连、后台扫描不主动断网、FLASH凭据和SNTP/RTC；UI错误不假称断网或密码必错。
退出取消列表异步回调，停下本页扫描并清SDK扫描记录，保留无线服务/连接；扫描取记录失败也清理SDK记录。

采用项目已有LVGL和SDK能力，未增加通用SVG渲染或配网依赖。2026-10-04核查：
[LVGL9.5键盘映射/默认处理器](https://docs.lvgl.io/9.5/widgets/keyboard.html#new-keymap)、
[ESP-IDF6.0.1扫描记录与释放契约](https://github.com/espressif/esp-idf/blob/v6.0.1/components/esp_wifi/include/esp_wifi.h)。
自定义的只有键盘排列和`#+=`转到额外符号页，其余按键继续委托`lv_keyboard_def_event_cb`。

`tests/wifi/wifi_ui_tests.c`使用真实LVGL与假无线API，覆盖中英状态、每键圆形边界、密码掩码/取消/重试、
全部ASCII、SSID/PSK长度、立即GOT_IP、认证失败/超时、扫描失败、开关和12次退出清理。
主机中的SSID、电量与连接结果均为夹具，不能代替设备真实连接验收。

## 地址数据与持久化

`weather_locations_data.c` 是只读 Flash 索引：稳定 ID、父索引、UTF-8 名称/拼音池、
WGS84 中心经纬度（E5）；索引 0 只表示根/无效项，不可选作天气地点。
来源为 MIT 授权的 [AreaCity](https://github.com/xiangyuecn/AreaCity-JsSpider-StatsGov)
`2025.251231.260403`，保留许可证与输入 SHA256 于 `artwork/locations`。
本次收录 33 个省级、372 个市级、2851 个区县级节点，共 3256 项。
这是该版本的大陆及港澳列表；来源没有台湾市/区坐标，海外仅为无坐标占位，均未提供选择。
数据版本更新前不能称为最新行政区划；也不提供 GPS 自动定位或乡镇级列表。

源坐标为 GCJ02，构建时按 MIT [coordtransform](https://github.com/wandergis/coordtransform)
的逆变换转换至 WGS84；不把国测坐标直接用于 Open-Meteo，也不按同名搜索的第一项猜地址。
运行端不解压边界或联网查询地名；约 115 KiB 的节点与字符串直接从 Flash 读取。
重新生成：解压源 release 的 `ok_data_level3.csv` / `ok_geo.csv` 为 `level3.csv` / `geo.csv`，
运行 `python3 tools/gen_weather_locations.py <CSV目录>`，再生成地点/天气字库。

`weather_locations.c` 延迟加载独立 NVS namespace `weather`，键 `locations` 为一个版本化
blob：版本、当前 ID、最近三个 ID。缺失/不认识的 ID 回到上海；省级项不可直接确认。
`wx_location_select` 成功写入/提交后才改变 RAM，失败保留当前地点并显示保存失败。
系统设置 namespace、旧键、分区格式均不迁移、不擦除。

## 交互与请求状态

`app_weather.c:weather_enter` 建正常天气内容和城市点击区；点击城市/红色 pin →
`weather_location_ui_open`，隐藏正常内容，打开最近/常用三项及“其他地点”。
最近行带省份辅助说明以区分同名地点。

“其他地点” → 单个三行滚轮按省 → 市 → 区县选择。`load_stage` 用真实父索引生成
子选项，父级变化会重新选择合法子项；页头/路径显示当前级别与已选路径。
没有下一级时可确认当前市级项。返回逐级退回，常用页再返回关闭；暂选从不保存。
`commit` 成功后异步删除自身输入对象，避免点击事件内销毁当前目标。

确认 → `wx_location_select` → `s_generation++` / `WX_IDLE` / 缓存失效 → 清空旧图标和
温度 → 恢复新地点标题 → `start_fetch`。请求快照在创建任务前固定坐标/代次；
旧任务完成时仅在 generation 相同时发布数据。新请求等旧任务释放后由 tick/poll 启动，
不会并行积累 HTTP 栈。`portMUX` 保护缓存和代次的发布/读取；工作任务不访问 LVGL/NVS。

`weather_tick` 与天气表盘读同一缓存；成功 20 分钟、失败 1 分钟刷新，失败文案可点重试。
HTTP 非 200、截断、缺关键字段/非有限数值显示失败，不把缺失天气解析为 0℃。
2026-10-04 新增 [天气详情滚动](./WEATHER_DETAILS_DESIGN.md)：固定城市点击区仍可从详情打开，地址切换重置滚动位置、停止动画并使全部详情失效；取消地址选择保留浏览位置。

`weather_ui_tests` 实际走南京秦淮的省市区选择及确认、城市标题触控区、取消、不落盘失败、
旧响应晚返回与缓存恢复；`location_tests` 检查全部父索引/坐标、最近去重、保存失败及重启读回。
`font_location_24` 为新增地址字库，24px/2bpp；既有天气正文 4bpp 保持，16px 字库新增省名覆盖。
