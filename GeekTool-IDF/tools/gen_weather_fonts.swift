// 天气页专用 4bpp 平滑字库;本地 CoreText,无网络/安装/外部写入。
// cd GeekTool-IDF && swift tools/gen_weather_fonts.swift
// 保留 ASCII、°、温度箭头与天气/i18n 文案用字;仅写 font_weather_{16,20}.c。
import Foundation
import CoreText
import CoreGraphics

var codes = Set<UInt32>(32...126)
codes.formUnion([0x00b0,0x2191,0x2193])
for source in ["main/weather_ui.c", "main/i18n.c"] {
    var text = try String(contentsOfFile: source, encoding: .utf8)
    if source.hasSuffix("i18n.c"), let start = text.range(of:"/* weather */"),
       let end = text.range(of:"/* stopwatch / countdown */") {
        text = String(text[start.upperBound..<end.lowerBound])
    }
    // 只收集字符串,不要把注释中的汉字也塞进天气字体。
    let pattern = try NSRegularExpression(pattern:#""(?:\\.|[^"\\])*""#)
    let whole = NSRange(text.startIndex..<text.endIndex,in:text)
    for match in pattern.matches(in:text,range:whole) {
        let literal = text[Range(match.range,in:text)!]
        for scalar in literal.unicodeScalars where (0x4e00...0x9fff).contains(scalar.value) {
            codes.insert(scalar.value)
        }
    }
}
for size in [16,20] {
    var sizeCodes=codes
    if size==16 {
        let provinces=try String(contentsOfFile:"artwork/locations/province-names.txt",encoding:.utf8)
        for c in provinces.unicodeScalars where (0x4e00...0x9fff).contains(c.value) {sizeCodes.insert(c.value)}
    }
    let height = size+6, baseline = 5
    var bitmap: [UInt8] = []
    var descriptors = ["{.bitmap_index=0, .adv_w=0, .box_w=0, .box_h=0, .ofs_x=0, .ofs_y=0}"]
    var chunks: [String] = []
    let ordered = sizeCodes.sorted()
    for code in ordered {
        let text = String(UnicodeScalar(code)!)
        let isCJK = (0x4e00...0x9fff).contains(code)
        let font = CTFontCreateWithName((isCJK ? "PingFangSC-Regular" : "HelveticaNeue") as CFString,CGFloat(size),nil)
        let attr = [kCTFontAttributeName:font,kCTForegroundColorAttributeName:CGColor(gray:1,alpha:1)] as CFDictionary
        let line = CTLineCreateWithAttributedString(CFAttributedStringCreate(nil,text as CFString,attr))
        let advance = CGFloat(CTLineGetTypographicBounds(line,nil,nil,nil))
        let width = max(2,Int(ceil(advance))+2)
        let context = CGContext(data:nil,width:width,height:height,bitsPerComponent:8,bytesPerRow:width,
                                space:CGColorSpaceCreateDeviceGray(),bitmapInfo:CGImageAlphaInfo.none.rawValue)!
        context.setAllowsAntialiasing(true)
        context.setShouldSmoothFonts(false)
        context.textPosition = CGPoint(x:1,y:baseline)
        CTLineDraw(line,context)
        let pixels = context.data!.bindMemory(to:UInt8.self,capacity:width*height)
        // Trim only pixels that are already transparent at 4bpp. Keep every alpha value,
        // advance and baseline position; otherwise whitespace consumes the OTA partition.
        var x0=width,y0=height,x1=0,y1=0
        for y in 0..<height {for x in 0..<width where pixels[y*width+x]>>4 != 0 {
            x0=min(x0,x);y0=min(y0,y);x1=max(x1,x+1);y1=max(y1,y+1)
        }}
        let glyphWidth=max(0,x1-x0),glyphHeight=max(0,y1-y0)
        var values:[UInt8]=[]
        if glyphWidth>0 && glyphHeight>0 {for y in y0..<y1 {for x in x0..<x1 {
            values.append(pixels[y*width+x]>>4)
        }}}
        var bytes: [UInt8] = []
        // fmt_txt 的 4bpp 字形是连续像素流,奇数宽度也不做逐行补齐。
        for i in stride(from:0,to:values.count,by:2) {
            bytes.append(values[i]<<4 | (i+1<values.count ? values[i+1] : 0))
        }
        let offset = bitmap.count
        bitmap.append(contentsOf:bytes)
        chunks.append(String(format:"/* U+%04X */",code)+"\n"+bytes.enumerated().map { index,value in
            String(format:"0x%02x,",value)+(index%20==19 ? "\n" : "")
        }.joined()+"\n")
        descriptors.append("{.bitmap_index=\(offset), .adv_w=\(Int((advance*16).rounded())), .box_w=\(glyphWidth), .box_h=\(glyphHeight), .ofs_x=\(glyphWidth>0 ? x0-1 : 0), .ofs_y=\(glyphHeight>0 ? height-y1-baseline : 0)}")
    }
    let first = ordered[0], last = ordered.last!
    let unicode = ordered.map { String($0-first) }.joined(separator:",")
    let output = """
    // 自动生成: tools/gen_weather_fonts.swift;天气页独立字库,\(size)px,4bpp。
    #include "lvgl.h"
    static LV_ATTRIBUTE_LARGE_CONST const uint8_t bitmap[] = {
    \(chunks.joined())};
    static const lv_font_fmt_txt_glyph_dsc_t glyphs[] = {
    \(descriptors.joined(separator:",\n"))};
    static const uint16_t unicode[] = {\(unicode)};
    static const lv_font_fmt_txt_cmap_t cmaps[] = {{
        .range_start=\(first), .range_length=\(last-first+1), .glyph_id_start=1,
        .unicode_list=unicode, .list_length=\(ordered.count), .type=LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    }};
    static const lv_font_fmt_txt_dsc_t dsc = {
        .glyph_bitmap=bitmap, .glyph_dsc=glyphs, .cmaps=cmaps, .cmap_num=1, .bpp=4
    };
    const lv_font_t font_weather_\(size) = {
        .get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt, .get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,
        .line_height=\(height), .base_line=\(baseline), .dsc=&dsc
    };
    """
    let path = "main/font_weather_\(size).c"
    try output.write(toFile:path,atomically:true,encoding:.utf8)
    print("\(path): \(ordered.count) glyphs / \(bitmap.count) bitmap bytes")
}
