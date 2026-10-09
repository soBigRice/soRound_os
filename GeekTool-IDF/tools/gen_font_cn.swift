// 生成中文精简字库 main/font_cn16.c(LVGL fmt_txt,16px 4bpp,苹方 PingFang SC)。
// 零外部依赖:macOS 自带 CoreText 渲染。保留历史字集并扫描现有页面文案。
// 仅裁掉量化后完全透明的外边缘,用字形偏移保留原有像素位置、字距和基线。
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
func isFallbackCharacter(_ value: UInt32) -> Bool {
    value == 0x00B7 || (0x4E00...0x9FFF).contains(value) || (0x3000...0x303F).contains(value) || (0xFF00...0xFFEF).contains(value)
}
for f in srcFiles {
    guard let s = try? String(contentsOfFile: f, encoding: .utf8) else {
        FileHandle.standardError.write("cannot read \(f)\n".data(using: .utf8)!); exit(1)
    }
    for ch in s where ("\u{4E00}"..."\u{9FFF}").contains(ch) { set.insert(ch) }
}
// System's private labels share this fallback, but comments are not product copy.
// Keep the added character set scoped to its actual string literals.
let literals = try NSRegularExpression(pattern:#""(?:\\.|[^"\\])*""#)
for filename in ["main/app_sys.c", "main/app_fluid.c", "main/app_maze.c", "main/maze_levels.c", "main/app_pixels.c"] {
    let text = try String(contentsOfFile:filename,encoding:.utf8)
    for match in literals.matches(in:text,range:NSRange(text.startIndex..<text.endIndex,in:text)) {
        for ch in text[Range(match.range,in:text)!] where isFallbackCharacter(ch.unicodeScalars.first!.value) {
            set.insert(ch)
        }
    }
}
// 文案索引不覆盖所有页面和历史字符。新增文案时保留已交付字集,
// 不能因扫描范围变窄,让其他页面或动态名称里的中文突然缺字。
if let previous = try? String(contentsOfFile: outPath, encoding: .utf8) {
    let regex = try NSRegularExpression(pattern: #"U\+([0-9A-Fa-f]{4,6})"#)
    let range = NSRange(previous.startIndex..<previous.endIndex, in: previous)
    for match in regex.matches(in: previous, range: range) {
        if let hex = Range(match.range(at: 1), in: previous),
           let value = UInt32(previous[hex], radix: 16), isFallbackCharacter(value),
           let scalar = UnicodeScalar(value) {
            set.insert(Character(String(scalar)))
        }
    }
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
        // CGBitmapContext 内存本就是顶行在前(绘图坐标系原点在左下,但存储不是)——直拷,勿翻转
        for i in 0..<(BOX * BOX) { gray[i] = p[i] }
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
    let ink = (0..<(BOX * BOX)).filter { g[$0] >> 4 != 0 }
    let x0 = ink.map { $0 % BOX }.min() ?? 0
    let y0 = ink.map { $0 / BOX }.min() ?? 0
    let x1 = (ink.map { $0 % BOX }.max() ?? -1) + 1
    let y1 = (ink.map { $0 / BOX }.max() ?? -1) + 1
    let width = x1 - x0, height = y1 - y0
    var pixels: [UInt8] = []
    for y in y0..<y1 { for x in x0..<x1 { pixels.append(g[y * BOX + x] >> 4) } }
    var bytes: [UInt8] = []
    // LVGL 4bpp is a continuous pixel stream, including across odd-width rows.
    for i in stride(from: 0, to: pixels.count, by: 2) {
        bytes.append((pixels[i] << 4) | (i + 1 < pixels.count ? pixels[i + 1] : 0))
    }
    bitmap.append(contentsOf: bytes)
    let u = ch.unicodeScalars.first!.value
    bmpTxt += String(format: "    /* U+%04X \"%@\" */\n    ", u, String(ch))
    bmpTxt += bytes.enumerated().map { (i, b) in String(format: "0x%02x,%@", b, (i % 16 == 15) ? "\n    " : " ") }.joined()
    bmpTxt += "\n\n"
    // Bottom-relative ofs_y keeps the ink at the same baseline-relative coordinates.
    dsc += "    {.bitmap_index = \(idx), .adv_w = \(BOX * 16), .box_w = \(width), .box_h = \(height), .ofs_x = \(x0), .ofs_y = \(ink.isEmpty ? 0 : BOX - y1)},\n"
}
let first = chars.first!.unicodeScalars.first!.value
let last = chars.last!.unicodeScalars.first!.value
let uniList = chars.map { String($0.unicodeScalars.first!.value - first) }.joined(separator: ", ")

let out = """
/*******************************************************************************
 * 中文精简字库(自动生成,勿手改)—— tools/gen_font_cn.swift
 * 苹方 PingFang SC \(Int(SIZE))px,4bpp,\(chars.count) 字;透明边缘无损裁剪,字距 \(BOX)px。
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
let cleanOutput = out.replacingOccurrences(of: #"(?m)[ \t]+$"#, with: "", options: .regularExpression)
try! cleanOutput.write(toFile: outPath, atomically: true, encoding: .utf8)
print("OK: \(outPath)  \(chars.count) glyphs, bitmap \(bitmap.count) bytes")
