# 六款极简字体 · 原生候选

2026-10-22：按用户要求将[六款原稿](../type-exploration-20261022/README.md)接入本地目录，ID44–49为STACK、AIR、OFFSET、COLUMN、CUT、EDITION。用户已确认设计并授权提交、官网部署及设备烧录，发布结果以表盘说明及验证回执为准。

![正常原生画面](overview.png)

独立OFL Barlow字库保留Black/Regular/SemiBold字重和正屏/半尺寸字号；日期读取真实快照，纯排版只按分钟重绘。AOD在大字绘制前返回通用低亮信息，不渲染大字号。原44款编号、字体文件和像素保持。

边界及真实选择器：[00:00](midnight-overview.png)、[23:59](late-overview.png)、[233px预览](preview-overview.png)、[中文设置](selector-44-zh.png)、[英文独立入口](app-selector-49-en.png)。正常参考固定2026-10-20 10:08:36，选择器为另一套固定夹具；所有时间、日期都是样例。

5/5相关主机测试、8组共16个字库的真实字形检查、1120组STUDIO整帧/分块比较、原44款134帧等价以及ESP32-S3构建通过；摘要见[validation.json](validation.json)。这不是新十二款的真机栈、RAM、功耗、帧率或主观验收。

复现：先运行`python3 -B GeekTool-IDF/tools/gen_watchface_fonts.py --typography`生成独立字体，使用临时主机构建的`watchface_tests`执行`python3 -B scripts/render_site_watchfaces.py --faces <binary> --review GeekTool-IDF/artwork/watchfaces/type-native-20261022 --review-from 44 --review-to 50`。导出仅做原生PPM→PNG转换；233px图来自真实选择器裁切，未缩放466px替代。结束后删除自己的临时构建和原始PPM。
