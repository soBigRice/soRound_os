// 编译真实挂载入口；pthread只替代FreeRTOS同步与Flash挂载服务。
// Compile the actual mount entry; pthread replaces only FreeRTOS synchronization and the Flash service.
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../../main/resource_store.c"

_Thread_local unsigned fixture_critical_depth;
static unsigned create_calls,mount_calls,take_calls;
static unsigned mount_failures,active_mounts,max_active_mounts;
static bool create_fail;
static TickType_t last_timeout;
static uint64_t monotonic_ms(void) {
    struct timespec time;assert(clock_gettime(CLOCK_MONOTONIC,&time)==0);
    return (uint64_t)time.tv_sec*1000u+(uint64_t)time.tv_nsec/1000000u;
}
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *storage) {
    assert(fixture_critical_depth==1);++create_calls;if(create_fail)return NULL;
    assert(!storage->initialized);assert(pthread_mutex_init(&storage->native,NULL)==0);storage->initialized=true;return storage;
}
int xSemaphoreTake(SemaphoreHandle_t mutex,TickType_t ticks) {
    // 统计字段也串行，便于TSAN检查真实入口的跨线程共享状态。
    // Serialize fixture counters too so TSAN can audit the real entry's shared state.
    portENTER_CRITICAL(&s_init_mux);++take_calls;last_timeout=ticks;portEXIT_CRITICAL(&s_init_mux);
    uint64_t started=monotonic_ms();
    while(true) {
        int error=pthread_mutex_trylock(&mutex->native);if(!error)return pdTRUE;
        assert(error==EBUSY);if(monotonic_ms()-started>=ticks)return pdFALSE;
        const struct timespec pause={.tv_nsec=1000000};nanosleep(&pause,NULL);
    }
}
void xSemaphoreGive(SemaphoreHandle_t mutex){assert(pthread_mutex_unlock(&mutex->native)==0);}
int esp_vfs_fat_spiflash_mount_ro(const char *base,const char *partition,const esp_vfs_fat_mount_config_t *config) {
    assert(fixture_critical_depth==0);
    assert(!strcmp(base,"/img")&&!strcmp(partition,"storage")&&config->max_files==2&&!config->format_if_mount_failed);
    ++mount_calls;++active_mounts;if(active_mounts>max_active_mounts)max_active_mounts=active_mounts;
    const struct timespec pause={.tv_nsec=5000000};nanosleep(&pause,NULL);
    --active_mounts;if(mount_failures){--mount_failures;return ESP_FAIL;}return ESP_OK;
}
static void reset(void) {
    assert(!active_mounts);
    if(s_mount_storage.initialized)assert(pthread_mutex_destroy(&s_mount_storage.native)==0);
    memset(&s_mount_storage,0,sizeof s_mount_storage);s_mount_mutex=NULL;s_mounted=false;
    create_calls=mount_calls=take_calls=mount_failures=active_mounts=max_active_mounts=0;create_fail=false;last_timeout=0;
}
static pthread_mutex_t gate_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_condition=PTHREAD_COND_INITIALIZER;
static bool gate_open;
static void *call_mount(void *output) {
    assert(pthread_mutex_lock(&gate_mutex)==0);
    while(!gate_open)assert(pthread_cond_wait(&gate_condition,&gate_mutex)==0);
    assert(pthread_mutex_unlock(&gate_mutex)==0);*(bool*)output=resource_store_mount();return NULL;
}
static void concurrent(unsigned expected_success,unsigned expected_mounts) {
    pthread_t threads[12];bool results[12]={0};gate_open=false;
    for(unsigned i=0;i<12;i++)assert(pthread_create(&threads[i],NULL,call_mount,&results[i])==0);
    assert(pthread_mutex_lock(&gate_mutex)==0);gate_open=true;assert(pthread_cond_broadcast(&gate_condition)==0);assert(pthread_mutex_unlock(&gate_mutex)==0);
    unsigned successes=0;for(unsigned i=0;i<12;i++){assert(pthread_join(threads[i],NULL)==0);successes+=results[i];}
    assert(successes==expected_success&&mount_calls==expected_mounts&&create_calls==1&&max_active_mounts==1);
}
int main(void) {
    create_fail=true;assert(!resource_store_mount()&&create_calls==1&&mount_calls==0);
    create_fail=false;assert(resource_store_mount()&&create_calls==2&&mount_calls==1);reset();
    mount_failures=1;assert(!resource_store_mount()&&!s_mounted&&mount_calls==1);
    assert(resource_store_mount()&&s_mounted&&mount_calls==2);assert(resource_store_mount()&&mount_calls==2);reset();
    concurrent(12,1);assert(last_timeout==2000);reset();
    mount_failures=1;concurrent(11,2);reset();
    assert(resource_store_mount());assert(pthread_mutex_lock(&s_mount_storage.native)==0);
    uint64_t started=monotonic_ms();assert(!resource_store_mount());uint64_t elapsed=monotonic_ms()-started;
    assert(elapsed>=1990&&elapsed<3500&&last_timeout==2000&&mount_calls==1);
    assert(pthread_mutex_unlock(&s_mount_storage.native)==0);assert(resource_store_mount()&&mount_calls==1);reset();
    puts("Shared read-only resource mount: create failure, retry,12-way races, cached reuse and bounded2000ms wait passed.");
    return 0;
}
