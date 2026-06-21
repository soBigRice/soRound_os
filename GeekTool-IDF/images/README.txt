图片表盘背景图放这里。

把一张图命名为 bg.jpg 放进本目录,然后:
    idf.py -C GeekTool-IDF flash      # 会把本目录打包成 storage 分区一起烧入

要求:466x466 的【基线(非渐进)】JPEG。一键转换(ImageMagick):
    magick 你的图.jpg -resize 466x466^ -gravity center -extent 466x466 \
        -interlace none -sampling-factor 4:2:0 GeekTool-IDF/images/bg.jpg

换图:替换 bg.jpg 后重新 flash 即可(只重烧 storage 分区也行)。
没有 bg.jpg 时,image 表盘会显示提示文字,不影响其它表盘。
