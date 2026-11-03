#pragma once
#include "mimi_assets.h"
#include <stdbool.h>
#include <stdint.h>

typedef uint16_t (*mimi_front_mix_fn)(uint16_t foreground,uint16_t background,unsigned q);
typedef struct {
    mimi_asset_span_t *spans;
    uint32_t rows[467];
    size_t count;
    unsigned max_row,old_max_depth,max_depth,sample_depth;
    uint64_t old_pixel_ops,pixel_ops,opaque_pixels,transparent_pixels;
    uint64_t unchanged_sequences,background_checks,exhaustive_background_checks;
} mimi_front_canonical_t;

// 仅离线生成：最后一次q32使前缀与背景无关，后缀仍按原RGB565混合顺序逐次舍入。
// Offline only: the final q32 removes prefix/background dependence, while the suffix retains native per-operation RGB565 rounding.
// 没有q32的像素序列必须逐元素不变；调用方还需按压缩体积与现有reader边界选择是否使用。
// Pixel sequences without q32 stay element-for-element identical; the caller must still apply compressed-size and reader-bound guards.
bool mimi_front_canonicalize(const mimi_asset_span_t *spans,const uint32_t rows[467],unsigned frame,
                             const uint16_t *background,const uint16_t *reference,mimi_front_mix_fn mix,
                             mimi_front_canonical_t *out);
void mimi_front_canonical_free(mimi_front_canonical_t *out);
