# 设置与天气地址交互

核对日期：2026-10-04。用户授权完成功能后已实现原生 LVGL 页面；主机回归和固件构建
见 `PORTING_NOTES.md` 本轮记录。USB 未识别设备后用户选择自行 OTA；已发布
[v1.7-beta.13](https://github.com/soBigRice/soRound_os/releases/tag/v1.7-beta.13)。圆屏观感、真实地址请求及侧倾触发条件仍需设备操作验收。
浏览器旧样稿位于 [artwork/settings-location/index.html](./artwork/settings-location/index.html)，
其中地址/天气仍为样例；固件没有使用该样例列表。

## 设置页

`main/app_settings.c:settings_enter` → `rebuild`：每屏一项，顺序为亮度、表盘、常显、
音量、静音、语言、关于。上下点阵箭头或内容区上下滑动切换，系统返回/右滑退出；
顶部系统热区下拉仍打开快捷面板。

- 亮度/音量：大点阵数字、点阵横条；`slider_changed` 实时应用，`LV_EVENT_RELEASED`
  才 `settings_save`。亮度仍为 `SETTINGS_BRIGHT_MIN=64` 至 255，音量 0 至 100。
  滑块不冒泡手势，避免横向调整触发返回。
- 常显/静音：点选立即应用并保存，沿用 `IDLE_AOD`/`IDLE_OFF` 及全局静音语义。
- 表盘：保留 `dots/bold/rings/weather/image` 五项，同页预览、左右选择；图片预览
  读取已预热的 `img_store_face_image`，天气预览使用原稿资源，选择仍经过 `watchface_select`。
- 语言立即保存并重画本页，关于显示真实 `esp_app_get_description()->version`。
- `queue_rebuild` 在输入事件结束后异步重建，退出取消待执行回调；音量页申请音频，
  `settings_exit` 释放，避免影响随后进入麦克风应用。

`launcher.c:header_app_style` 只为设置和天气采用紧凑标题/返回按钮，其他 App 样式恢复。
页面固定画布为 466×466，文字与触控区留在圆屏内；`settings_level_tests` 检查双语字形、
圆形安全范围、控件保存时机、开关、表盘预览、异步销毁与音频释放。

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
