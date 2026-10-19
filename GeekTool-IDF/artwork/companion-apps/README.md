# Pocket Garden / Focus Deck 素材

2026-10-19 使用内置 `image_gen`（imagegen skill）生成两张独立生产素材，已经实际查看原图及 466×466 的真实 LVGL 显示。选定视觉保持原先生态箱与唱片机方向，没有重新选风格。

保留：`garden-source.png` / `focus-source.png` 是未经覆盖的生成原件；`*-device.png` 是可重建的设备尺寸透明 PNG；`native/` 是实际 App 原生渲染的精选状态。纯黑画布不会直接作为旋转的有色背景：转换脚本只把近纯黑外底透明化，其他像素保留 RGB565 颜色，独立 A8 平面用于 LVGL tint / 旋转。

`native/` 的外圈 74% 电量是 host 审阅 fixture，不是读取设备的结果；屏幕中的专注分钟与新叶来自测试中真实执行的状态流。主启动器返回按钮由集成层提供，不包含在这组局部截图中。

转换：从 `GeekTool-IDF` 执行 `python3 tools/gen_companion_artwork.py`，Pillow 只进行裁除生成黑色外边、等比缩小和像素格式编码；原件保留。盆景二像素安全边、阈值 15 / alpha 8；唱片阈值 12 / alpha 8。目标 334×238 与 256×256；不拉伸、不裁掉有效主体。固件资源为 RGB565+A8，合计 435,084 B。

生成来源：内置输出 `/Users/superrice/.codex/generated_images/01a1543a-ce9c-76a2-ab0c-5747674e606d/exec-835befb9-7622-49f9-9c55-31f0a5928152.png` 与 `exec-bc5f784e-b249-4def-9a94-350eb59c44a1.png`；项目直接引用本目录副本，不依赖个人生成目录。

## Garden prompt

> Use case: stylized-concept. Asset type: production bitmap sprite for a 466px round AMOLED embedded device. Make one standalone miniature pixel-art Japanese pocket garden on PURE BLACK (#000000) background, no UI or text, no buttons, no border. A beautifully twisted bonsai pine with detailed layered emerald and lime foliage above mossy stone islands, a tiny warm lantern on the lower left, a bright clear turquoise pond at lower center/right, small grasses and a tiny pale bird on a stone. Three-quarter view as a lovingly crafted isometric game asset, hard pixel clusters and careful highlights, richly detailed but readable at 330x230 pixels. Composition is a wide 3:2 isolated low island, object fully visible, generous black margin all around. The reference's established visual direction is refined pixel art floating on black; keep the canopy airy and asymmetrical, trunk clearly visible. No floor plane outside island, no gradients or typography, no screen mockup. Output square canvas with the garden centered.

## Focus prompt

> Use case: stylized-concept. Production bitmap asset for a refined embedded focus timer UI. A single perfectly circular black vinyl record photographed perfectly top down, isolated centered on absolute pure black background. Full disc visible, approximately 90 percent of frame width. Rich fine concentric grooves, delicate charcoal-silver highlights from upper left, subtle real material texture, center circle is warm vermilion red with a very slight paper texture and no printing at all, red label diameter about 38 percent of record. Small black spindle dot precisely in center. Keep disc perfectly circular and symmetrical. No tonearm, no letters, no numbers, no UI, no drop shadow outside disc, no hands, no player case, no buttons. Luxurious analog texture, dark restrained palette and genuinely beautiful groove detail, readable at 284px diameter. Square canvas, pure black corners.
