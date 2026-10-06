# 木鱼素材

2026-10-06，使用内置 imagegen 生成，非第三方下载素材。`source.png` 保留1536×1024原件；`tools/gen_merit_artwork.py` 等比例转换为288×192、quality=95、4:4:4基线JPEG，并生成 `main/merit_artwork.c`。不裁剪有效内容、不重绘雕纹或开口。设备沿用已有JPEG解码器与原生RGB565像素格式。`wooden-fish.jpg` 为31,446B。

最终提示词：

> Use case: stylized-concept. Asset type: wooden fish instrument sprite for a small 466x466 circular AMOLED meditation app. Create ONE beautiful, instantly recognizable traditional Chinese Buddhist wooden fish percussion instrument (木鱼 / mokugyo), without a mallet. Three-quarter front view, slightly from above. Authentic carved camphor wood temple-block form: squat rounded hollow body, broad horizontal slit opening in the front with deeply black interior, carved curled fish head and paired prominent round fish eyes above the slit, swirling scale and fin relief around the sides, curved ridged crown. Rich amber walnut wood, visible grain and warm sculpted highlights, a tactile polished hand-carved 3D product illustration. Calm elegant object, visually readable even at 260 pixels. Pure solid black #000000 backdrop. No pedestal or cushion, no glow, no floor, no drop shadow outside object, no text or symbols, no decorative objects. Landscape canvas 3:2. Object alone centered, fully contained and filling approximately 86% canvas width and 80% height; leave a small clear black margin all around. Carefully shaped silhouette and authentic deep resonant mouth slit, do not depict an actual fish, drum, bowl, pebble, or generic oval.

木槌与敲击动效由 `app_merit.c/wood_draw` 独立绘制，便于运动；静止素材不含木槌。计数、声音开关与提示属于真实LVGL控件，不烘焙在图片中。原生主机渲染已核对，用户主观效果和真实AMOLED显示待验收。
