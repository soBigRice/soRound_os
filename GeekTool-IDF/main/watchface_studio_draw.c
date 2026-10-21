#include "watchface_studio_draw.h"
#include <stdlib.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

// 全部STUDIO编译单元共用一个有界工作区，避免深层对象绘制栈重复承载扫描数组。
// All STUDIO translation units share one bounded workspace instead of placing scan arrays on the deep object draw stack.
wfs_draw_scratch_t g_wfs_draw_scratch;
wfs_span_t g_wfs_spans[4];
_Static_assert(sizeof(g_wfs_spans)==40,"STUDIO span batching must stay bounded to 40 bytes");
_Static_assert(sizeof(g_wfs_draw_scratch)==2084,"STUDIO scratch must remain bounded to 2084 bytes");
static wfs_geometry_t *s_geometry;
static bool s_geometry_attempted;
wfs_geometry_t *wfs_geometry_cache(void) {
    // 不随时间变化的原始浮点路径只算一次；有界PSRAM缓存不存像素，也不影响采样精度。
    // Compute time-invariant float paths once; bounded PSRAM geometry stores no pixels and changes no sampling precision.
    if(!s_geometry_attempted){
        s_geometry_attempted=true;
#ifdef ESP_PLATFORM
        s_geometry=heap_caps_calloc(1,sizeof *s_geometry,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
        s_geometry=calloc(1,sizeof *s_geometry);
#endif
    }
    return s_geometry;
}
void wfs_geometry_release(void) {
#ifdef ESP_PLATFORM
    heap_caps_free(s_geometry);
#else
    free(s_geometry);
#endif
    s_geometry=NULL;s_geometry_attempted=false;
}
