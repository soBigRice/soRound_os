#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
static bool fail;
static unsigned allocations,live;
static void *allocate(size_t count,size_t bytes){++allocations;if(fail)return NULL;void *p=calloc(count,bytes);assert(p);++live;return p;}
static void release(void *p){if(p){assert(live==1);--live;free(p);}}
#define calloc allocate
#define free release
#include "../../main/watchface_studio_draw.c"
#undef calloc
#undef free
int main(void){
    fail=true;assert(!wfs_geometry_cache() && allocations==1);
    for(int i=0;i<100;i++)assert(!wfs_geometry_cache());
    assert(allocations==1);
    wfs_geometry_release();fail=false;
    wfs_geometry_t *cache=wfs_geometry_cache();assert(cache && live==1 && sizeof *cache==13924 && !cache->iris_ready && !cache->growth_ready);
    cache->growth_ready=1;cache->growth[0][0]=(wfs_point_t){12.34f,56.78f};
    for(int i=0;i<1000;i++)assert(wfs_geometry_cache()==cache && cache->growth[0][0].x==12.34f);
    assert(allocations==2);wfs_geometry_release();assert(!live);
    cache=wfs_geometry_cache();assert(!cache->growth_ready && !cache->iris_ready);wfs_geometry_release();assert(!live);
    puts("STUDIO geometry: bounded 13924-byte cache, single allocation, failure latch, exact float reuse and release/re-entry passed.");
}
