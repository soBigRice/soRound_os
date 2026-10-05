图片表盘背景图放这里。

把一张图命名为 bg.jpg 放进本目录,然后:
    idf.py -C GeekTool-IDF flash      # 会把本目录打包成 storage 分区一起烧入

要求:466x466 的【基线(非渐进)】JPEG。一键转换(ImageMagick):
    magick 你的图.jpg -resize 466x466^ -gravity center -extent 466x466 \
        -interlace none -sampling-factor 4:2:0 GeekTool-IDF/images/bg.jpg

换图:替换 bg.jpg 后重新 flash 即可(只重烧 storage 分区也行)。
没有 bg.jpg 或无法解码时,image 表盘使用所选 TYPE / ORBIT / SHIFT 的固件内置默认背景。
三个图片款均优先使用用户的 bg.jpg；AOD 隐藏照片保留时间，不会覆盖本目录原文件。
