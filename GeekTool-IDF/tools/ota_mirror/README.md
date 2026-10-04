# OTA 服务器镜像

## 目标与边界

GitHub Actions 继续将正式版和 beta 包发布到 `geektool-fw` R2 桶；镜像服务器约每分钟
从独立 R2 HTTPS 源检查并拉取最新包。设备仍使用 `ota.miaozong.cc`，无需修改或先升级固件。
正式版只替换 `GeekTool.bin`，beta 通道也接受正式版，保持
[`firmware.yml`](../../../.github/workflows/firmware.yml) 的现有发布规则。

仅清理镜像目录内的旧包/中断临时文件，不删除 GitHub Release 历史、R2 桶内其他对象，
也不删除设备另一个 OTA 分区的启动回滚镜像。文件覆盖后旧 inode 的现有读取可以完成，
其空间在最后一个读取关闭后释放；磁盘目录最终只保留两个当前 `.bin`。

## 调用链与失败行为

`geektool-ota-sync.timer` → `sync_firmware.py:Mirror.run()` 获取排他文件锁 →
分别执行 `sync_channel(stable/beta)`：

1. HEAD 获取 `Content-Length` 和强 ETag；使用新查询参数与 `no-cache` 避免拉到旧缓存。
2. 源地址、ETag、本地大小和本地 SHA-256 全部一致时跳过 GET。
3. GET 使用 HEAD 的 `If-Match`，要求完整、未压缩响应及相同 ETag/大小。
4. 在同一文件系统临时下载；单次 socket 等待 15 秒，下载总期限 180 秒，每个镜像不超过
   `partitions.csv` 的 `0x300000` OTA 槽。
5. `verify_image()` 检查 ESP32-S3 芯片 ID、段边界、校验和、附加 SHA-256、应用描述中的
   `GeekTool` 项目及版本。正式通道拒绝 `-beta`。当前发布为附加哈希的未签名镜像，
   不引入新的签名契约；将来启用 Secure Boot 时需先更新镜像校验器。
6. fsync 后原子替换对应公开文件，再持久化独立通道状态。下载、源变化、校验失败均不
   替换公开文件；状态写入失败时文件仍是已校验镜像，下次会重新核对。一个通道失败不阻塞另一个。

Nginx/OpenResty 直接读取静态文件，支持 Content-Length、Range 和条件请求。
关闭文件缓存和 gzip，避免覆盖后继续提供旧 inode 或改变字节范围；响应 `no-store`。
Nginx 静态 ETag 使用修改时间及大小，所以每次替换将 mtime 至少增加一秒，防止同大小镜像
在同秒替换时复用 ETag。设备现有 `ota_update.c` 的 Range/If-Match 及镜像身份检查保持不变。

## 部署前提与顺序

2026-10-04 已部署到用户指定的个人主页服务器 `154.37.221.172`，使用已有 1Panel 2.3.2
和 OpenResty 1.31.1.1；没有变更主页、Pingoo、数据库或其他站点。服务器自身的 IP 查询
返回 HK/Hong Kong，所以它是本次指定的香港直连节点，**不将其描述为大陆服务器，也不
把电脑下载结果当成设备提速验收**。

已添加同一个 `geektool-fw` 桶的独立源 `r2-ota.miaozong.cc`，服务器从该源实际下载并
校验正式版/beta。设备域名 `ota.miaozong.cc` 已解除旧 R2 绑定，改为 `154.37.221.172`
的 A 记录并关闭代理（DNS only）。Cloudflare、阿里公共 DNS、腾讯公共 DNS 均返回该 IP，
没有 AAAA；主域名/www/通配符的原代理状态保持原配置。

切换前以 `curl --resolve` 验证，切换后以普通公网域名再次验证：可信 HTTPS、连接 IP、
两个完整镜像 SHA-256、HEAD 长度及强 ETag/no-store、Range 206 和字节内容、错误 If-Match
412、POST 403、状态/临时路径 404 全部通过。专用下载配置经过目标机 `nginx -t` 后 reload；
原站点配置保存在 `/opt/geektool-ota/ota-vhost-before-1791083370019994234.conf`。
自动同步 timer 已启用并设置开机启动；11:23、11:24 的真实定时执行均成功，两个通道
版本未变时仅核对，不重复下载。公开目录只有两份当前 `.bin`，没有下载临时文件；
1Panel 自动生成的 `index.html`/`404.html` 保留但不对外开放。
OTA 专用证书 ID 5 正常，有效期至 2027-01-02；已保存 HTTP 验证及自动续签。
HTTP-01 路径与 1Panel 2.3.2 的 `UseHTTP(httpRoot)` 对齐，实际公开探针返回 200；
探针已移回 `/tmp`，首次 DNS-01 的临时 TXT 记录已移除。未来续期执行尚未发生。
真机 OTA 下载速度、安装、重启仍待设备验收。

模板可用于普通服务器；此次 1Panel 部署通过 `install_1panel.py` 适配已有 `/opt/1panel/www → /www` 挂载：

| 路径 | 内容 |
| --- | --- |
| `/opt/geektool-ota/sync_firmware.py` | root 管理的同步程序 |
| `/opt/1panel/www/sites/ota.miaozong.cc/index` | `geektool-ota` 用户可写，容器内 `/www/sites/ota.miaozong.cc/index` 可读 |
| `/var/lib/geektool-ota` | systemd StateDirectory；通道状态及同步锁，不公开 |
| `/etc/geektool-ota/source.env` | `OTA_R2_SOURCE=https://r2-ota.miaozong.cc`；不需要 R2 密钥 |
| `/etc/systemd/system/geektool-ota-sync.{service,timer}` | 每次结束后 60 秒再次同步 |

1. 确认服务器和已有站点、容器挂载、Python/systemd、80/443 端口；保留原 DNS/R2 绑定和
   相关配置用于回退，不修改其他服务。
2. 给同一个 R2 桶增加独立源域名 `r2-ota.miaozong.cc`，确认 HTTPS 与两个对象可访问。
   不使用生产环境会限流的 `r2.dev`。`--source-base` 禁止等于设备域名，禁止源重定向，
   防止迁移后服务器从自身拉包。
3. 建专用用户与上述目录，安装程序和 systemd 文件，先手动启动同步，核对两份镜像哈希、
   版本和状态。只存在 HEAD 元数据不足以切换。
4. 配置独立 `ota.miaozong.cc` HTTPS 站点，使用 `nginx-locations.conf` 中的下载配置。
   OpenResty 若在容器内，路径必须有对应可读挂载；优先适配已有站点目录，避免修改无关容器。
   使用公开可信的证书（可用 DNS-01 提前签发），不能用仅 Cloudflare 代理信任的 Origin CA。
5. 先 `nginx -t`，再 reload。通过 `curl --resolve ota.miaozong.cc:443:服务器IP` 验证完整
   下载、Range 206、匹配 If-Match 与错误 ETag 返回 412，且与 R2/Release 哈希一致。
6. 确认上述步骤后才移除旧 OTA R2 域名绑定，将 `ota.miaozong.cc` 改为镜像服务器 A 记录，
   **DNS only（灰云）**；旧 AAAA 若指向 Cloudflare 必须一并处理，避免 IPv6 仍走海外。
   R2 独立源域名保留 Cloudflare 分发。启用 timer，核对公网 DNS、TLS、Range 和完整哈希。
7. 下次 Actions 发布仍上传 R2；核对 timer 拉到新版本、旧版本被替换，设备 OTA 下载/启动
   由真机验收。出现迁移问题可停止 timer，并恢复原 R2 域名绑定/DNS，不影响发布存储。

## 1Panel 安装与维护

`install_1panel.py install` 创建无登录 shell 的专用系统用户，安装 root 管理的同步程序、
source.env 和 service/timer。service 显式传入主机公开目录，并限制为只写公开目录及
StateDirectory；先启动同步，核对两通道本地文件和持久化状态一致。

在 1Panel 创建 `ota.miaozong.cc` 静态站点并启用其受信任证书后，运行
`install_1panel.py configure-web`。该操作只改此域名的 vhost，保留 1Panel 管理的证书路径，
保留 80 端口的 HTTP-01 验证路径，其余 HTTP 请求跳转 HTTPS；HTTPS 仅公开两个固件路径。
先保存配置副本，再原子写入；`nginx -t` 失败会恢复旧文件，成功才 reload。
公网 HTTPS/Range/哈希验证后运行 `install_1panel.py enable-timer`。

常用核对命令：

```bash
systemctl list-timers geektool-ota-sync.timer --no-pager
journalctl -u geektool-ota-sync.service -n 20 --no-pager
systemctl start geektool-ota-sync.service
```

网页终端在本次操作中丢失 `_`、花括号等字符：不在它里面键入多行配置或嵌入代码。
本次用 1Panel 上传已检查的归档，核对 SHA-256 后解包，执行简单命令；文件名含 `_` 时
用 shell 补全并在执行前核对显示内容。不要把传输字符错误误判为 Python/服务器故障。

## 验证与来源

从仓库根目录执行：

```bash
python3 -m unittest discover -s GeekTool-IDF/tools/ota_mirror -p 'test_*.py' -v
```

2026-10-04：12 个同步回归通过，包括中断/超时/损坏保留当前包、源 ETag 变化、稳定通道
保护、通道独立失败、同秒同大小替换的 ETag 身份、旧文件读取继续完成及本地损坏修复。
另用此前已下载并对照发布资产的真实镜像校验：

| 通道 | 版本 | 字节数 | SHA-256 |
| --- | --- | --- | --- |
| 正式 | `v1.6.1` | 1,872,192 | `703e4e5ff3c0b3b63baeaf45fcfaa73ae0a9d07e47028f9d73ae1b205a120807` |
| beta | `v1.7-beta.13` | 3,113,424 | `b34b630938f80ab0c9cdd378a021954189e26d9bd4253abe7e9d98d307adb2b5` |

本地临时官方 Nginx 容器（`nginx:stable-alpine`，digest
`sha256:0985e772fb9f729e6fa0980da05fca5d9c468e870eed43071545afa9d2e27d94`）加载本目录
下载配置，`nginx -t` 通过；两个真实镜像均验证 HEAD 大小/强 ETag/no-store、Range 206
及字节内容、错误 If-Match 返回 412、完整 GET 的上述 SHA-256。状态/临时路径返回 404，
POST 返回 403。测试容器已停止并移除。此检查不覆盖目标机 OpenResty 版本、TLS 或公网 DNS。

镜像规则按 ESP-IDF 6.0.1 `esp_app_desc.h` 和本机 esptool 实现核对；静态下载复用现有
Nginx/OpenResty，不另建下载 API，不改固件分区或发布协议。镜像服务只读公开 R2 源，
无需给服务器保存 R2 写入密钥。

- [Espressif 固件镜像格式](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/firmware-image-format.html)
- [R2 公共桶和自定义域名](https://developers.cloudflare.com/r2/buckets/public-buckets/)
- [R2 域名绑定 API](https://developers.cloudflare.com/api/resources/r2/subresources/buckets/subresources/domains/subresources/custom/methods/create/)
- [1Panel 2.3.2 证书申请、更新与 HTTP 根目录](https://github.com/1Panel-dev/1Panel/blob/v2.3.2/agent/app/service/website_ssl.go)
- [1Panel 证书申请与 HTTP 自动续期](https://1panel.cn/docs/v2/user_manual/websites/certificate_create/)
- [Nginx 静态 ETag、Range、sendfile](https://nginx.org/en/docs/http/ngx_http_core_module.html)
