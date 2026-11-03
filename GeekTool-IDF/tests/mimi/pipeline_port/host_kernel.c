#include "test_hooks.h"
#include "esp_heap_caps.h"
#include "esp_pm.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static pthread_mutex_t books=PTHREAD_MUTEX_INITIALIZER;
static test_resources_t resources;
static unsigned cap_attempts,queue_attempts,task_attempts,cap_fail,queue_fail,task_fail;
static unsigned power_attempts,power_fail;
static atomic_uint done_writes;
typedef union {
    max_align_t alignment;
    struct {size_t size;uint32_t magic;unsigned caps;} info;
} block_t;
#define BLOCK_MAGIC 0xc01dcafeu

void test_fail_allocations(unsigned cap,unsigned queue,unsigned task){
    pthread_mutex_lock(&books);
    assert(!resources.cap_blocks&&!resources.queues&&!resources.tasks&&!resources.power_locks&&!resources.held_power_locks);
    cap_attempts=queue_attempts=task_attempts=0;cap_fail=cap;queue_fail=queue;task_fail=task;
    power_attempts=power_fail=0;
    atomic_store(&done_writes,0);pthread_mutex_unlock(&books);
}
test_resources_t test_resources(void){
    pthread_mutex_lock(&books);test_resources_t result=resources;pthread_mutex_unlock(&books);return result;
}
unsigned test_done_overwrites(void){return atomic_load(&done_writes);}
void test_fail_power_allocation(unsigned attempt){pthread_mutex_lock(&books);assert(!resources.power_locks);power_fail=attempt;pthread_mutex_unlock(&books);}
void *heap_caps_malloc(size_t bytes,unsigned caps){
    assert(caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)||caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    pthread_mutex_lock(&books);unsigned attempt=++cap_attempts;bool fail=cap_fail&&attempt==cap_fail;pthread_mutex_unlock(&books);
    if(fail||bytes>SIZE_MAX-sizeof(block_t))return NULL;
    block_t *block=malloc(sizeof *block+bytes);if(!block)return NULL;
    block->info.size=bytes;block->info.magic=BLOCK_MAGIC;block->info.caps=caps;
    pthread_mutex_lock(&books);resources.cap_blocks++;resources.cap_bytes+=bytes;pthread_mutex_unlock(&books);
    return block+1;
}
void *heap_caps_calloc(size_t count,size_t bytes,unsigned caps){
    if(count&&bytes>SIZE_MAX/count)return NULL;
    void *memory=heap_caps_malloc(count*bytes,caps);if(memory)memset(memory,0,count*bytes);return memory;
}
void heap_caps_free(void *memory){
    if(!memory)return;
    block_t *block=(block_t*)memory-1;assert(block->info.magic==BLOCK_MAGIC);block->info.magic=0;
    pthread_mutex_lock(&books);assert(resources.cap_blocks&&resources.cap_bytes>=block->info.size);resources.cap_blocks--;resources.cap_bytes-=block->info.size;pthread_mutex_unlock(&books);free(block);
}

struct test_power_lock {bool held;};
esp_err_t esp_pm_lock_create(int kind,int argument,const char *name,esp_pm_lock_handle_t *out){
    assert(kind==ESP_PM_CPU_FREQ_MAX&&argument==0&&name&&out);*out=NULL;
    pthread_mutex_lock(&books);bool fail=power_fail&&++power_attempts==power_fail;pthread_mutex_unlock(&books);
    if(fail)return ESP_FAIL;
    esp_pm_lock_handle_t lock=calloc(1,sizeof *lock);if(!lock)return ESP_FAIL;
    pthread_mutex_lock(&books);resources.power_locks++;pthread_mutex_unlock(&books);*out=lock;return ESP_OK;
}
esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t lock){
    assert(lock);pthread_mutex_lock(&books);assert(!lock->held);lock->held=true;resources.held_power_locks++;pthread_mutex_unlock(&books);return ESP_OK;
}
esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t lock){
    assert(lock);pthread_mutex_lock(&books);assert(lock->held&&resources.held_power_locks);lock->held=false;resources.held_power_locks--;pthread_mutex_unlock(&books);return ESP_OK;
}
esp_err_t esp_pm_lock_delete(esp_pm_lock_handle_t lock){
    assert(lock);pthread_mutex_lock(&books);assert(!lock->held&&resources.power_locks);resources.power_locks--;pthread_mutex_unlock(&books);free(lock);return ESP_OK;
}

struct test_task {
    pthread_t thread;pthread_mutex_t mutex;pthread_cond_t condition;
    TaskFunction_t function;void *argument;eTaskState state;bool deleting;unsigned received_jobs;
};
static _Thread_local TaskHandle_t self;
unsigned test_current_job(void){assert(self);return self->received_jobs;}
static void set_state(eTaskState state){if(self){pthread_mutex_lock(&self->mutex);self->state=state;pthread_mutex_unlock(&self->mutex);}}
static void *run_task(void *argument){
    TaskHandle_t task=argument;self=task;set_state(eRunning);task->function(task->argument);set_state(eDeleted);return NULL;
}
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn,const char *name,uint32_t stack,void *arg,UBaseType_t priority,TaskHandle_t *out,BaseType_t core){
    assert(fn&&name&&stack&&priority&&out&&core==0);*out=NULL;
    pthread_mutex_lock(&books);unsigned attempt=++task_attempts;bool fail=task_fail&&attempt==task_fail;pthread_mutex_unlock(&books);
    if(fail)return pdFAIL;
    TaskHandle_t task=calloc(1,sizeof *task);if(!task)return pdFAIL;
    assert(!pthread_mutex_init(&task->mutex,NULL));assert(!pthread_cond_init(&task->condition,NULL));
    task->function=fn;task->argument=arg;task->state=eReady;
    int error=pthread_create(&task->thread,NULL,run_task,task);
    if(error){pthread_cond_destroy(&task->condition);pthread_mutex_destroy(&task->mutex);free(task);return pdFAIL;}
    pthread_mutex_lock(&books);resources.tasks++;pthread_mutex_unlock(&books);*out=task;return pdPASS;
}
TickType_t xTaskGetTickCount(void){struct timespec time;assert(!clock_gettime(CLOCK_MONOTONIC,&time));return (TickType_t)((uint64_t)time.tv_sec*1000+time.tv_nsec/1000000);}
void vTaskDelay(TickType_t ticks){
    struct timespec duration={(time_t)(ticks/1000),(long)(ticks%1000)*1000000};
    set_state(eBlocked);while(nanosleep(&duration,&duration)&&errno==EINTR){}set_state(eRunning);
}
void vTaskSuspend(TaskHandle_t task){
    if(!task)task=self;
    assert(task&&task==self);pthread_mutex_lock(&task->mutex);task->state=eSuspended;pthread_cond_broadcast(&task->condition);
    while(!task->deleting)pthread_cond_wait(&task->condition,&task->mutex);
    pthread_mutex_unlock(&task->mutex);pthread_exit(NULL);
}
void vTaskDelete(TaskHandle_t task){
    assert(task&&task!=self);pthread_mutex_lock(&task->mutex);assert(task->state==eSuspended);task->deleting=true;task->state=eDeleted;pthread_cond_broadcast(&task->condition);pthread_mutex_unlock(&task->mutex);
    assert(!pthread_join(task->thread,NULL));pthread_cond_destroy(&task->condition);pthread_mutex_destroy(&task->mutex);
    pthread_mutex_lock(&books);assert(resources.tasks);resources.tasks--;pthread_mutex_unlock(&books);free(task);
}
eTaskState eTaskGetState(TaskHandle_t task){assert(task);pthread_mutex_lock(&task->mutex);eTaskState state=task->state;pthread_mutex_unlock(&task->mutex);return state;}
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task){
    assert(task||self);
    // 只测试退出回执的字段传递，不把pthread的模拟值当成ESP任务栈水位。
    // Test stop-ack field delivery only; this sentinel is not an ESP stack measurement.
    return TEST_STACK_SENTINEL;
}

struct test_queue {
    pthread_mutex_t mutex;pthread_cond_t condition;size_t bytes;unsigned ordinal,waiters;bool occupied;uint8_t data[];
};
QueueHandle_t xQueueCreate(UBaseType_t length,size_t bytes){
    assert(length==1&&bytes);pthread_mutex_lock(&books);unsigned ordinal=++queue_attempts;bool fail=queue_fail&&ordinal==queue_fail;pthread_mutex_unlock(&books);
    if(fail||bytes>SIZE_MAX-sizeof(struct test_queue))return NULL;
    QueueHandle_t queue=calloc(1,sizeof *queue+bytes);if(!queue)return NULL;
    assert(!pthread_mutex_init(&queue->mutex,NULL));assert(!pthread_cond_init(&queue->condition,NULL));queue->bytes=bytes;queue->ordinal=ordinal;
    pthread_mutex_lock(&books);resources.queues++;pthread_mutex_unlock(&books);return queue;
}
BaseType_t xQueueOverwrite(QueueHandle_t queue,const void *item){
    assert(queue&&item);pthread_mutex_lock(&queue->mutex);memcpy(queue->data,item,queue->bytes);queue->occupied=true;
    if(queue->ordinal==2)atomic_fetch_add(&done_writes,1);
    pthread_cond_broadcast(&queue->condition);pthread_mutex_unlock(&queue->mutex);return pdPASS;
}
BaseType_t xQueueReceive(QueueHandle_t queue,void *item,TickType_t ticks){
    assert(queue&&item);pthread_mutex_lock(&queue->mutex);
    struct timespec deadline={0};if(ticks&&ticks!=portMAX_DELAY){assert(!clock_gettime(CLOCK_REALTIME,&deadline));deadline.tv_sec+=ticks/1000;deadline.tv_nsec+=(long)(ticks%1000)*1000000;if(deadline.tv_nsec>=1000000000){deadline.tv_sec++;deadline.tv_nsec-=1000000000;}}
    while(!queue->occupied){
        if(!ticks){pthread_mutex_unlock(&queue->mutex);return pdFALSE;}
        queue->waiters++;set_state(eBlocked);
        int error=ticks==portMAX_DELAY?pthread_cond_wait(&queue->condition,&queue->mutex):pthread_cond_timedwait(&queue->condition,&queue->mutex,&deadline);
        set_state(eRunning);queue->waiters--;
        if(error==ETIMEDOUT){pthread_mutex_unlock(&queue->mutex);return pdFALSE;}
        assert(!error);
    }
    memcpy(item,queue->data,queue->bytes);queue->occupied=false;
    if(queue->ordinal==1&&self)self->received_jobs++;
    pthread_mutex_unlock(&queue->mutex);return pdTRUE;
}
void vQueueDelete(QueueHandle_t queue){
    assert(queue);pthread_mutex_lock(&queue->mutex);assert(!queue->waiters);pthread_mutex_unlock(&queue->mutex);
    pthread_cond_destroy(&queue->condition);pthread_mutex_destroy(&queue->mutex);
    pthread_mutex_lock(&books);assert(resources.queues);resources.queues--;pthread_mutex_unlock(&books);free(queue);
}
