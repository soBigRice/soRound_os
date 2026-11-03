#include "mimi_pipeline.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#ifdef ESP_PLATFORM
#include "esp_pm.h"
#endif
#include <limits.h>
#include <stddef.h>
#include <string.h>

typedef struct { mimi_pet_t pet;uint32_t slot,index,epoch,serial;bool stop; } job_t;
typedef struct { uint32_t slot,index,epoch,serial,stack_free,error;bool stopped; } result_t;
struct mimi_pipeline {
    mimi_assets_t *assets;
    mimi_pipeline_frame_t *frames;
    int16_t *delta;
    QueueHandle_t jobs,done;
    TaskHandle_t worker;
#ifdef ESP_PLATFORM
    esp_pm_lock_handle_t boost;
#endif
    job_t submit;
    uint32_t epoch,clip,serial,last_ms;
    int active,pending;
    bool closing,failed;
    mimi_pipeline_stats_t stats;
};
static void boost(mimi_pipeline_t *p,bool acquire){
#ifdef ESP_PLATFORM
    if(acquire)esp_pm_lock_acquire(p->boost);else esp_pm_lock_release(p->boost);
#else
    (void)p;(void)acquire;
#endif
}
static bool body_row(void *context,unsigned row,uint8_t *pixels){
    return mimi_assets_body_row(context,row,pixels);
}
static void worker(void *argument){
    mimi_pipeline_t *p=argument;job_t job;
    for(;;){
        if(xQueueReceive(p->jobs,&job,portMAX_DELAY)!=pdTRUE)continue;
        if(job.stop){
            result_t stopped={.stack_free=(uint32_t)uxTaskGetStackHighWaterMark(NULL),.stopped=true};
            xQueueOverwrite(p->done,&stopped);
            // STOP确认后先挂起，由UI观察挂起状态并删除，禁止跨核提前释放工作区。
            // Suspend after STOP acknowledgement; the UI observes suspension before deleting cross-core resources.
            for(;;)vTaskSuspend(NULL);
        }
        boost(p,true);
        mimi_pipeline_frame_t *frame=&p->frames[job.slot];
        frame->snapshot=job.pet;frame->epoch=job.epoch;frame->serial=job.serial;
        uint32_t error=0;
        if(!mimi_assets_load(p->assets,job.index,&frame->asset))error=MIMI_PIPELINE_ERROR_ASSET;
        else if(frame->asset.frame.index!=job.index||frame->asset.frame.clip!=job.pet.motion||
                frame->asset.frame.pose_ms!=job.pet.motion_ms)error=MIMI_PIPELINE_ERROR_PROTOCOL;
        else if(!mimi_prepare_eyes_rows(&job.pet,job.pet.motion_ms,body_row,&frame->asset.frame,p->delta,&frame->eyes))
            error=MIMI_PIPELINE_ERROR_PREPARE;
        boost(p,false);
        result_t result={.slot=job.slot,.index=job.index,.epoch=job.epoch,.serial=job.serial,
            .stack_free=(uint32_t)uxTaskGetStackHighWaterMark(NULL),.error=error};
        xQueueOverwrite(p->done,&result);
    }
}
static void release(mimi_pipeline_t *p){
    if(p->jobs)vQueueDelete(p->jobs);
    if(p->done)vQueueDelete(p->done);
#ifdef ESP_PLATFORM
    if(p->boost)esp_pm_lock_delete(p->boost);
#endif
    mimi_assets_close(p->assets);
    heap_caps_free(p->frames);heap_caps_free(p->delta);heap_caps_free(p);
}
mimi_pipeline_t *mimi_pipeline_create(const char *path){
    if(!path)return NULL;
    mimi_pipeline_t *p=heap_caps_calloc(1,sizeof *p,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(!p)return NULL;
    p->active=p->pending=-1;p->epoch=1;p->clip=UINT32_MAX;
    p->assets=mimi_assets_open(path);
    if(!p->assets){release(p);return NULL;}
    p->frames=heap_caps_calloc(2,sizeof *p->frames,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    p->delta=heap_caps_malloc((MIMI_ASSET_SIDE+1)*sizeof *p->delta,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    p->jobs=xQueueCreate(1,sizeof(job_t));p->done=xQueueCreate(1,sizeof(result_t));
    if(!p->assets||!p->frames||!p->delta||!p->jobs||!p->done){release(p);return NULL;}
#ifdef ESP_PLATFORM
    if(esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX,0,"mimi_prepare",&p->boost)!=ESP_OK){release(p);return NULL;}
#endif
    // 沿用已测小样的8192字节任务栈；解压状态及输出均在堆中，不占此栈。
    // Retain the measured8192byte pilot stack; inflater state and output live on the heap.
    if(xTaskCreatePinnedToCore(worker,"mimi_prepare",8192,p,5,&p->worker,0)!=pdPASS){release(p);return NULL;}
    return p;
}
static bool same_snapshot(const mimi_pet_t *old,const mimi_pet_t *current,uint32_t pose_ms){
    size_t at=offsetof(mimi_pet_t,motion_ms),after=at+sizeof current->motion_ms;
    // motion_ms使用量化时刻比较，其余完整快照逐字节比较；无需在UI栈上再复制一份大结构。
    // Compare quantized motion time and every other snapshot byte without another large UI-stack copy.
    return old->motion_ms==pose_ms&&!memcmp(old,current,at)&&
           !memcmp((const uint8_t*)old+after,(const uint8_t*)current+after,sizeof *current-after);
}
static void submit(mimi_pipeline_t *p,unsigned index,uint32_t pose_ms,const mimi_pet_t *pet){
    if(p->pending>=0||p->closing||p->failed)return;
    if(p->serial&&p->submit.epoch==p->epoch&&p->submit.index==index&&same_snapshot(&p->submit.pet,pet,pose_ms))return;
    p->submit.slot=p->active==0?1:0;p->submit.index=index;p->submit.epoch=p->epoch;
    p->submit.serial=++p->serial;p->submit.stop=false;
    // 精确复制对象表示，保证早轮询比较的padding与原实时状态一致。
    // Preserve the object representation so early-poll comparisons retain the live state's padding too.
    memcpy(&p->submit.pet,pet,sizeof *pet);p->submit.pet.motion_ms=pose_ms;
    p->pending=(int)p->submit.slot;
    if(xQueueOverwrite(p->jobs,&p->submit)!=pdPASS){
        p->pending=-1;p->failed=true;p->stats.errors|=MIMI_PIPELINE_ERROR_QUEUE;
    }
}
const mimi_pipeline_frame_t *mimi_pipeline_update(mimi_pipeline_t *p,const mimi_pet_t *pet,bool *new_frame){
    if(new_frame)*new_frame=false;
    if(!p||!pet||p->closing||p->failed)return NULL;
    unsigned index;uint32_t pose_ms;
    if(!pet->motion_active||!mimi_assets_find(p->assets,pet->motion,pet->motion_ms,&index,&pose_ms)){
        if(p->clip!=UINT32_MAX)mimi_pipeline_invalidate(p);
        return NULL;
    }
    // 同动作重新开始也属于新代际，不能发布上一轮尾部的迟到帧。
    // Restarting the same clip creates a new epoch, so a late tail frame from its previous run cannot appear.
    if(p->clip!=pet->motion||(p->clip!=UINT32_MAX&&pet->motion_ms<p->last_ms)){
        if(p->clip!=UINT32_MAX)p->epoch++;
        p->clip=pet->motion;
    }
    p->last_ms=pet->motion_ms;
    result_t result;
    if(xQueueReceive(p->done,&result,0)==pdTRUE){
        if(result.stopped||result.slot>1||p->pending!=(int)result.slot||
           result.serial!=p->submit.serial||result.epoch!=p->submit.epoch||result.index!=p->submit.index){
            p->failed=true;p->stats.errors|=MIMI_PIPELINE_ERROR_PROTOCOL;return NULL;
        }
        p->pending=-1;
        if(result.stack_free&&(!p->stats.worker_stack_free||result.stack_free<p->stats.worker_stack_free))
            p->stats.worker_stack_free=result.stack_free;
        if(result.error){p->failed=true;p->stats.errors|=result.error;return NULL;}
        // 时间可能已前进数帧；同代际/动作的完整结果仍可显示，不追赶丢弃而饿死。
        // The clock may advance several frames; display complete same-epoch/clip work rather than starving through chasing.
        mimi_pipeline_frame_t *frame=&p->frames[result.slot];
        if(result.epoch==p->epoch&&frame->asset.frame.clip==p->clip){
            p->active=(int)result.slot;p->stats.presented++;if(new_frame)*new_frame=true;
        }
    }
    submit(p,index,pose_ms,pet);
    if(p->failed)return NULL;
    if(p->active>=0){
        mimi_pipeline_frame_t *frame=&p->frames[p->active];
        if(frame->epoch==p->epoch&&frame->asset.frame.clip==p->clip)return frame;
    }
    p->stats.not_ready++;return NULL;
}
void mimi_pipeline_invalidate(mimi_pipeline_t *p){
    if(p&&!p->closing){p->epoch++;p->clip=UINT32_MAX;}
}
void mimi_pipeline_stats(const mimi_pipeline_t *p,mimi_pipeline_stats_t *out){
    if(out)*out=p?p->stats:(mimi_pipeline_stats_t){0};
}
bool mimi_pipeline_destroy(mimi_pipeline_t *p,mimi_pipeline_stats_t *out){
    if(!p){if(out)*out=(mimi_pipeline_stats_t){.stop_ok=1};return true;}
    if(!p->closing){p->closing=true;job_t stop={.stop=true};xQueueOverwrite(p->jobs,&stop);}
    TickType_t started=xTaskGetTickCount();
    while(eTaskGetState(p->worker)!=eSuspended){
        if(xTaskGetTickCount()-started>=pdMS_TO_TICKS(1000)){
            p->stats.errors|=MIMI_PIPELINE_ERROR_STOP;if(out)*out=p->stats;return false;
        }
        vTaskDelay(1);
    }
    result_t stopped;
    if(xQueueReceive(p->done,&stopped,0)==pdTRUE&&stopped.stopped)p->stats.worker_stack_free=stopped.stack_free;
    else p->stats.errors|=MIMI_PIPELINE_ERROR_PROTOCOL;
    vTaskDelete(p->worker);p->worker=NULL;p->stats.stop_ok=1;
    if(out)*out=p->stats;
    release(p);return true;
}
