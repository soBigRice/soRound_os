// 生成中文精简字库 main/font_cn16.c(LVGL fmt_txt,16px 4bpp,苹方 PingFang SC)。
// 零外部依赖:macOS 自带 CoreText 渲染。字表 = 扫描 main/ 下含中文串的源文件里的全部 CJK 字符
// (含注释,略胖无妨;新增中文文案后重跑本脚本再编译)。
// 用法:cd GeekTool-IDF && swift tools/gen_font_cn.swift
import Foundation
import CoreText
import CoreGraphics

let SIZE: CGFloat = 15.0        // 字号(渲染进 16x16 盒)
let BOX = 16                    // 盒宽高(px)
let srcFiles = ["main/i18n.c", "main/app_settings.c"]   // 扫这些文件里的 CJK
let outPath = "main/font_cn16.c"

// ---- 1) 收集字符集(CJK 统一表意区)----
var set = Set<Character>()
for f in srcFiles {
    guard let s = try? String(contentsOfFile: f, encoding: .utf8) else {
        FileHandle.standardError.write("cannot read \(f)\n".data(using: .utf8)!); exit(1)
    }
    for ch in s where ("\u{4E00}"..."\u{9FFF}").contains(ch) { set.insert(ch) }
}
let chars = set.sorted { $0.unicodeScalars.first!.value < $1.unicodeScalars.first!.value }
guard !chars.isEmpty else { print("no CJK found"); exit(1) }

// ---- 2) CoreText 渲染每字 → 16x16 灰度 → 4bpp ----
let font = CTFontCreateWithName("PingFangSC-Regular" as CFString, SIZE, nil)
let ascent = CTFontGetAscent(font), descent = CTFontGetDescent(font)

func render(_ ch: Character) -> [UInt8] {           // 返回 BOX*BOX 灰度(0-255,行主序,顶行在前)
    var gray = [UInt8](repeating: 0, count: BOX * BOX)
    let cs = CGColorSpaceCreateDeviceGray()
    guard let ctx = CGContext(data: nil, width: BOX, height: BOX, bitsPerComponent: 8,
                              bytesPerRow: BOX, space: cs, bitmapInfo: CGImageAlphaInfo.none.rawValue) else { return gray }
    ctx.setAllowsAntialiasing(true)
    ctx.setShouldSmoothFonts(false)
    let attr = [kCTFontAttributeName: font, kCTForegroundColorAttributeName: CGColor(gray: 1, alpha: 1)] as CFDictionary
    let line = CTLineCreateWithAttributedString(CFAttributedStringCreate(nil, String(ch) as CFString, attr))
    let w = CGFloat(CTLineGetTypographicBounds(line, nil, nil, nil))
    // 水平居中;竖直:基线放在使 ascent/descent 居中于盒(CG 原点在左下)
    let bx = (CGFloat(BOX) - w) / 2
    let by = (CGFloat(BOX) - (ascent + descent)) / 2 + descent
    ctx.textPosition = CGPoint(x: bx, y: by)
    CTLineDraw(line, ctx)
    if let data = ctx.data {
        let p = data.bindMemory(to: UInt8.self, capacity: BOX * BOX)
        for y in 0..<BOX {                           // CG 第 0 行是底部 → 翻转成顶行在前
            for x in 0..<BOX { gray[y * BOX + x] = p[(BOX - 1 - y) * BOX + x] }
        }
    }
    return gray
}

// ---- 3) 组装 C 文件 ----
var bitmap: [UInt8] = []
var dsc = "    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id 0 reserved */,\n"
var bmpTxt = ""
for ch in chars {
    let g = render(ch)
    let idx = bitmap.count
    var bytes: [UInt8] = []
    var i = 0
    while i < BOX * BOX {                            // 4bpp,两像素一字节,高半字节在前
        bytes.append(UInt8((g[i] & 0xF0) | (g[i + 1] >> 4)))
        i += 2
    }
    bitmap.append(contentsOf: bytes)
    let u = ch.unicodeScalars.first!.value
    bmpTxt += String(format: "    /* U+%04X \"%@\" */\n    ", u, String(ch))
    bmpTxt += bytes.enumerated().map { (i, b) in String(format: "0x%02x,%@", b, (i % 16 == 15) ? "\n    " : " ") }.joined()
    bmpTxt += "\n\n"
    dsc += "    {.bitmap_index = \(idx), .adv_w = \(BOX * 16), .box_w = \(BOX), .box_h = \(BOX), .ofs_x = 0, .ofs_y = 0},\n"
}
let first = chars.first!.unicodeScalars.first!.value
let last = chars.last!.unicodeScalars.first!.value
let uniList = chars.map { String($0.unicodeScalars.first!.value - first) }.joined(separator: ", ")

let out = """
/*******************************************************************************
 * 中文精简字库(自动生成,勿手改)—— tools/gen_font_cn.swift
 * 苹方 PingFang SC \(Int(SIZE))px,4bpp,\(chars.count) 字,盒 \(BOX)x\(BOX)。
 * 作为 unscii/montserrat 的 fallback 挂在 i18n.c;新增中文文案后重跑脚本。
 ******************************************************************************/
#include "lvgl.h"

static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
\(bmpTxt)};

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
\(dsc)};

static const uint16_t unicode_list_0[] = { \(uniList) };

static const lv_font_fmt_txt_cmap_t cmaps[] = {
    {
        .range_start = \(first), .range_length = \(last - first + 1), .glyph_id_start = 1,
        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL,
        .list_length = \(chars.count), .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    }
};

static const lv_font_fmt_txt_dsc_t font_dsc = {
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
};

const lv_font_t font_cn16 = {
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = 17,          /* 与 unscii_16 对齐,混排基线一致 */
    .base_line = 0,
    .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = 0,
    .underline_thickness = 0,
    .dsc = &font_dsc,
};
"""
try! out.write(toFile: outPath, atomically: true, encoding: .utf8)
print("OK: \(outPath)  \(chars.count) glyphs, bitmap \(bitmap.count) bytes")
