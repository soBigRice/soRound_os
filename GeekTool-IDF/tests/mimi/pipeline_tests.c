#include "mimi_pipeline.h"
#include "mimi_motion.h"
#include "test_hooks.h"
#include "esp_heap_caps.h"
#include "freertos/task.h"
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 这里只替换资源IO，队列/所有权测试运行真实生产后台与准备器；资源格式另由assets测试覆盖。
// Only resource I/O is replaced here; real production queues/preparation run, while asset-format tests cover decoding.
struct mimi_assets { unsigned marker; };
static pthread_t ui_thread;
static pthread_mutex_t gate_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_condition=PTHREAD_COND_INITIALIZER;
static bool gate_closed;
static unsigned entered,last_index;
static atomic_uint fill_value,delay_ms,open_files;
static atomic_bool load_fails;
static unsigned frames,ownership_checks,clip_switches,invalidations,allocation_failures,shutdowns;

mimi_assets_t *mimi_assets_open(const char *path){
    assert(pthread_equal(pthread_self(),ui_thread));
    if(!strcmp(path,"missing"))return NULL;
    mimi_assets_t *assets=heap_caps_calloc(1,sizeof *assets,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(assets){assets->marker=0x12345678;atomic_fetch_add(&open_files,1);}return assets;
}
void mimi_assets_close(mimi_assets_t *assets){
    if(!assets)return;assert(pthread_equal(pthread_self(),ui_thread));assert(test_resources().tasks==0);
    assert(assets->marker==0x12345678&&atomic_fetch_sub(&open_files,1)==1);heap_caps_free(assets);
}
bool mimi_assets_find(const mimi_assets_t *assets,unsigned clip,uint32_t ms,unsigned *index,uint32_t *pose){
    assert(assets&&assets->marker==0x12345678);unsigned offset,count;
    if(clip==10){offset=0;count=50;}else if(clip==11){offset=50;count=78;}else if(clip==13){offset=128;count=58;}else return false;
    unsigned n=ms/50;if(n>=count)n=count-1;*index=offset+n;*pose=n*50;return true;
}
bool mimi_assets_load(mimi_assets_t *assets,unsigned index,mimi_asset_slot_t *slot){
    assert(!pthread_equal(pthread_self(),ui_thread)&&assets&&index<186&&slot);unsigned value=atomic_load(&fill_value);
    pthread_mutex_lock(&gate_mutex);entered++;last_index=index;pthread_cond_broadcast(&gate_condition);
    while(gate_closed)pthread_cond_wait(&gate_condition,&gate_mutex);pthread_mutex_unlock(&gate_mutex);
    unsigned delay=atomic_load(&delay_ms);if(delay)vTaskDelay(delay);
    if(atomic_load(&load_fails))return false;
    unsigned clip=index<50?10:index<128?11:13,offset=clip==10?0:clip==11?50:128;
    slot->data[0]=(uint8_t)value;
    slot->frame=(mimi_asset_frame_t){.index=index,.clip=clip,.pose_ms=(index-offset)*50,
        .base_rows=slot->base_rows,.span_rows=slot->span_rows,.body_rle=slot->data,.base_rle=slot->data,
        .span_data=slot->data,.body_bytes=1,.base_bytes=1,.body_rows=slot->body_rows};
    return true;
}
bool mimi_assets_body_row(const mimi_asset_frame_t *frame,unsigned row,uint8_t out[466]){
    assert(!pthread_equal(pthread_self(),ui_thread)&&frame&&row<466);memset(out,frame->body_rle[0],466);return true;
}
static void assert_clean(void){
    test_resources_t r=test_resources();assert(!r.cap_blocks&&!r.cap_bytes&&!r.queues&&!r.tasks&&!r.power_locks&&!r.held_power_locks);assert(!atomic_load(&open_files));
}
static void reset(void){
    assert_clean();test_fail_allocations(0,0,0);pthread_mutex_lock(&gate_mutex);entered=last_index=0;gate_closed=false;pthread_mutex_unlock(&gate_mutex);
    atomic_store(&fill_value,255);atomic_store(&delay_ms,0);atomic_store(&load_fails,false);
}
static void gate(bool closed){pthread_mutex_lock(&gate_mutex);gate_closed=closed;pthread_cond_broadcast(&gate_condition);pthread_mutex_unlock(&gate_mutex);}
static unsigned entries(void){pthread_mutex_lock(&gate_mutex);unsigned result=entered;pthread_mutex_unlock(&gate_mutex);return result;}
static void await_entry(unsigned after){TickType_t start=xTaskGetTickCount();while(entries()<=after){assert(xTaskGetTickCount()-start<2000);vTaskDelay(1);}}
static void await_done(unsigned count){TickType_t start=xTaskGetTickCount();while(test_done_overwrites()<count){assert(xTaskGetTickCount()-start<2000);vTaskDelay(1);}}
static mimi_pet_t snapshot(unsigned clip,unsigned ms){mimi_pet_t pet;mimi_pet_init(&pet);mimi_pet_play(&pet,clip);pet.motion_ms=ms;return pet;}
static const mimi_pipeline_frame_t *next_frame(mimi_pipeline_t *p,const mimi_pet_t *pet,uint32_t after){
    TickType_t start=xTaskGetTickCount();
    for(;;){bool fresh=false;const mimi_pipeline_frame_t *frame=mimi_pipeline_update(p,pet,&fresh);if(frame&&fresh&&frame->serial>after)return frame;assert(xTaskGetTickCount()-start<2000);vTaskDelay(1);}
}
static uint64_t hash_frame(const mimi_pipeline_frame_t *frame){
    uint64_t hash=1469598103934665603ull;const unsigned char *data=(const unsigned char*)frame;
    for(size_t n=0;n<sizeof *frame;n++){hash^=data[n];hash*=1099511628211ull;}return hash;
}
static void check_frame(const mimi_pipeline_frame_t *frame,unsigned value){
    assert(frame&&frame->asset.frame.clip==frame->snapshot.motion&&frame->asset.frame.pose_ms==frame->snapshot.motion_ms);
    uint8_t *body=malloc(466*466);mimi_prepared_eyes_t *expected=malloc(sizeof *expected);assert(body&&expected);memset(body,(int)value,466*466);
    int16_t delta[467];assert(mimi_prepare_eyes(&frame->snapshot,frame->asset.frame.pose_ms,body,delta,expected));assert(!memcmp(expected,&frame->eyes,sizeof *expected));free(expected);free(body);
}
static void shutdown_ok(mimi_pipeline_t *p,unsigned allowed){
    mimi_pipeline_stats_t stats;assert(mimi_pipeline_destroy(p,&stats)&&stats.stop_ok==1&&stats.worker_stack_free==TEST_STACK_SENTINEL&&!(stats.errors&~allowed));shutdowns++;assert_clean();
}
static void sequence_and_ownership(void){
    reset();atomic_store(&delay_ms,2);mimi_pipeline_t *p=mimi_pipeline_create("mock");assert(p);mimi_pet_t pet=snapshot(11,0);
    const mimi_pipeline_frame_t *active=next_frame(p,&pet,0);check_frame(active,255);
    for(unsigned n=1;n<=260;n++){
        uint64_t hash=hash_frame(active);uint32_t old_epoch=active->epoch,old_serial=active->serial;
        pet=snapshot(11,(n*50)%3900);bool fresh,rewind=pet.motion_ms<active->snapshot.motion_ms;
        assert(mimi_pipeline_update(p,&pet,&fresh)==(rewind?NULL:active)&&!fresh);
        for(unsigned i=0;i<3;i++){assert(hash_frame(active)==hash);ownership_checks++;vTaskDelay(1);}
        const mimi_pipeline_frame_t *next=next_frame(p,&pet,old_serial);
        assert(next!=active&&next->epoch>=old_epoch);check_frame(next,255);active=next;frames++;
    }
    mimi_pipeline_stats_t stats;mimi_pipeline_stats(p,&stats);assert(stats.presented==261&&!stats.errors);shutdown_ok(p,0);
}
static void late_publication_and_copied_snapshot(void){
    reset();gate(true);mimi_pipeline_t *p=mimi_pipeline_create("mock");assert(p);
    mimi_pet_t original=snapshot(11,617),saved=original;bool fresh=true;assert(!mimi_pipeline_update(p,&original,&fresh)&&!fresh);await_entry(0);
    original.gaze_x=NAN;original.eyes[0][0].x=NAN;
    mimi_pet_t ahead=snapshot(11,2100);for(unsigned i=0;i<30;i++)assert(!mimi_pipeline_update(p,&ahead,&fresh));
    gate(false);const mimi_pipeline_frame_t *late=next_frame(p,&ahead,0);
    assert(late->asset.frame.pose_ms==600&&late->snapshot.gaze_x==saved.gaze_x&&late->snapshot.eyes[0][0].x==saved.eyes[0][0].x);check_frame(late,255);
    shutdown_ok(p,0);
}
static void clip_epoch_and_hidden_freeze(void){
    reset();mimi_pipeline_t *p=mimi_pipeline_create("mock");assert(p);mimi_pet_t pet=snapshot(10,700);const mimi_pipeline_frame_t *active=next_frame(p,&pet,0);
    unsigned prior=entries();gate(true);pet.motion_ms+=50;
    for(;;){bool fresh;const mimi_pipeline_frame_t *f=mimi_pipeline_update(p,&pet,&fresh);if(f)active=f;if(entries()>prior)break;vTaskDelay(1);}
    uint64_t hash=hash_frame(active);uint32_t old_epoch=active->epoch;mimi_pipeline_invalidate(p);invalidations++;
    pet=snapshot(13,1173);bool fresh;assert(!mimi_pipeline_update(p,&pet,&fresh));assert(hash_frame(active)==hash);
    atomic_store(&fill_value,0);gate(false);const mimi_pipeline_frame_t *next=next_frame(p,&pet,0);
    assert(next!=active&&next->epoch==old_epoch+1&&next->asset.frame.clip==13&&next->asset.frame.pose_ms==1150);check_frame(next,0);
    // 快速切clip使旧完成帧失效，但同clip时钟前进不会丢弃所有完成帧。
    // Clip changes cancel old completions, while advancing time within a clip must still allow publication.
    uint32_t random=0x88776655;const unsigned clips[]={10,11,13};
    for(unsigned i=0;i<100;i++){
        random=random*1664525u+1013904223u;unsigned clip=clips[(random>>16)%3];pet=snapshot(clip,(random>>8)%2000);pet.clock=i+1;
        const mimi_pipeline_frame_t *frame=next_frame(p,&pet,next->serial);assert(frame->asset.frame.clip==clip);check_frame(frame,0);next=frame;clip_switches++;
    }
    shutdown_ok(p,0);
}
static void same_clip_restart_drops_old_tail(void){
    reset();gate(true);mimi_pipeline_t *p=mimi_pipeline_create("mock");assert(p);
    mimi_pet_t tail=snapshot(11,3800);bool fresh;assert(!mimi_pipeline_update(p,&tail,&fresh));await_entry(0);
    mimi_pet_t restarted=snapshot(11,0);assert(!mimi_pipeline_update(p,&restarted,&fresh));gate(false);
    const mimi_pipeline_frame_t *frame=next_frame(p,&restarted,0);assert(frame->epoch==2&&frame->asset.frame.pose_ms==0);check_frame(frame,255);shutdown_ok(p,0);
}
static void duplicate_quantized_snapshot_is_idle(void){
    reset();mimi_pipeline_t *p=mimi_pipeline_create("mock");assert(p);mimi_pet_t pet=snapshot(11,617);
    const mimi_pipeline_frame_t *active=next_frame(p,&pet,0);uint32_t serial=active->serial;assert(entries()==1);
    pet.motion_ms=649;
    for(unsigned i=0;i<100;i++){bool fresh=true;assert(mimi_pipeline_update(p,&pet,&fresh)==active&&!fresh);vTaskDelay(1);}
    mimi_pipeline_stats_t stats;mimi_pipeline_stats(p,&stats);assert(entries()==1&&stats.presented==1);
    pet.gaze_x=.3f;const mimi_pipeline_frame_t *changed=next_frame(p,&pet,serial);assert(entries()==2&&changed->snapshot.gaze_x==.3f);
    serial=changed->serial;mimi_pipeline_invalidate(p);const mimi_pipeline_frame_t *renewed=next_frame(p,&pet,serial);assert(entries()==3&&renewed->epoch==2);
    serial=renewed->serial;pet.motion_ms=640;renewed=next_frame(p,&pet,serial);assert(entries()==4&&renewed->epoch==3);shutdown_ok(p,0);
}
static void failure_latches_and_arguments(void){
    reset();assert(!mimi_pipeline_create(NULL)&&!mimi_pipeline_create("missing"));assert_clean();
    bool fresh=true;mimi_pet_t pet=snapshot(11,600);assert(!mimi_pipeline_update(NULL,&pet,&fresh)&&!fresh);mimi_pipeline_invalidate(NULL);
    mimi_pipeline_stats_t stats;mimi_pipeline_stats(NULL,&stats);assert(!memcmp(&stats,&(mimi_pipeline_stats_t){0},sizeof stats));assert(mimi_pipeline_destroy(NULL,&stats)&&stats.stop_ok);
    mimi_pipeline_t *p=mimi_pipeline_create("mock");assert(p);assert(!mimi_pipeline_update(p,NULL,&fresh));
    mimi_pet_t ordinary=snapshot(1,500);assert(!mimi_pipeline_update(p,&ordinary,&fresh));
    atomic_store(&load_fails,true);assert(!mimi_pipeline_update(p,&pet,&fresh));await_done(1);assert(!mimi_pipeline_update(p,&pet,&fresh));
    mimi_pipeline_stats(p,&stats);assert(stats.errors==MIMI_PIPELINE_ERROR_ASSET);unsigned count=entries();atomic_store(&load_fails,false);
    for(unsigned i=0;i<20;i++){assert(!mimi_pipeline_update(p,&pet,&fresh));vTaskDelay(1);}assert(entries()==count);shutdown_ok(p,MIMI_PIPELINE_ERROR_ASSET);
}
static void oom(void){
    reset();for(unsigned i=1;i<=4;i++){test_fail_allocations(i,0,0);assert(!mimi_pipeline_create("mock"));assert_clean();allocation_failures++;}
    for(unsigned i=1;i<=2;i++){test_fail_allocations(0,i,0);assert(!mimi_pipeline_create("mock"));assert_clean();allocation_failures++;}
    test_fail_allocations(0,0,1);assert(!mimi_pipeline_create("mock"));assert_clean();allocation_failures++;
    test_fail_allocations(0,0,0);test_fail_power_allocation(1);assert(!mimi_pipeline_create("mock"));assert_clean();allocation_failures++;reset();
}
static void *release_gate(void *unused){(void)unused;vTaskDelay(20);gate(false);return NULL;}
static void shutdown_states_and_timeout(void){
    for(unsigned i=0;i<12;i++){
        reset();mimi_pipeline_t *p=mimi_pipeline_create("mock");assert(p);mimi_pet_t pet=snapshot(11,i*50);
        if(i%3==1){bool fresh;assert(!mimi_pipeline_update(p,&pet,&fresh));}
        if(i%3==2){next_frame(p,&pet,0);pet.motion_ms+=50;bool fresh;mimi_pipeline_update(p,&pet,&fresh);await_done(2);}shutdown_ok(p,0);
    }
    reset();gate(true);mimi_pipeline_t *p=mimi_pipeline_create("mock");assert(p);mimi_pet_t pet=snapshot(13,500);bool fresh;assert(!mimi_pipeline_update(p,&pet,&fresh));await_entry(0);
    pthread_t releaser;assert(!pthread_create(&releaser,NULL,release_gate,NULL));shutdown_ok(p,0);assert(!pthread_join(releaser,NULL));
    reset();gate(true);p=mimi_pipeline_create("mock");assert(p);assert(!mimi_pipeline_update(p,&pet,&fresh));await_entry(0);
    mimi_pipeline_stats_t stats;TickType_t began=xTaskGetTickCount();assert(!mimi_pipeline_destroy(p,&stats)&&xTaskGetTickCount()-began>=1000&&stats.errors&MIMI_PIPELINE_ERROR_STOP);
    test_resources_t r=test_resources();assert(r.cap_blocks==4&&r.queues==2&&r.tasks==1&&r.power_locks==1&&r.held_power_locks==1&&atomic_load(&open_files)==1);
    assert(!mimi_pipeline_update(p,&pet,&fresh));gate(false);shutdown_ok(p,MIMI_PIPELINE_ERROR_STOP);
}
int main(void){
    ui_thread=pthread_self();sequence_and_ownership();late_publication_and_copied_snapshot();clip_epoch_and_hidden_freeze();same_clip_restart_drops_old_tail();duplicate_quantized_snapshot_is_idle();failure_latches_and_arguments();oom();shutdown_states_and_timeout();
    assert_clean();printf("published_sequence_frames=%u active_ownership_checks=%u clip_switches=%u hidden_epoch_tests=%u restart_epoch_tests=1 repeated_poll_checks=100 shutdowns=%u allocation_failure_paths=%u late_publication=pass copied_snapshot=pass failed_io_latched=pass timeout_retention=pass resources=zero simulated_stack_field_only=true\n",frames,ownership_checks,clip_switches,invalidations,shutdowns,allocation_failures);return 0;
}
