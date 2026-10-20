#include "watchface_studio_draw.h"

// 全部STUDIO编译单元共用一个有界工作区，避免深层对象绘制栈重复承载扫描数组。
// All STUDIO translation units share one bounded workspace instead of placing scan arrays on the deep object draw stack.
wfs_draw_scratch_t g_wfs_draw_scratch;
_Static_assert(sizeof(g_wfs_draw_scratch)==2084,"STUDIO scratch must remain bounded to 2084 bytes");
