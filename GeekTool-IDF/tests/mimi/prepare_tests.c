#include "mimi_prepare.h"
#include "mimi_motion.h"
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mimi_expressions.inc"

void prepare_reference_body(const mimi_pet_t *,uint32_t,uint8_t *);
void prepare_reference_eyes(const mimi_pet_t *,uint32_t,const uint8_t *,mimi_prepared_eyes_t *);
_Static_assert(sizeof(mimi_prepared_eye_bounds_t)==12,"Eye metadata layout");
_Static_assert(sizeof(mimi_prepared_eyes_t)==86552,"Prepared eye allocation contract");
static unsigned cases,row_cases,clip_samples,body_reads;
static const float gazes[][2]={{-1,-1},{-1,1},{1,-1},{1,1},{0,0},{.1234567f,-.7654321f}};
static const float lids[]={0,.01333333f,.5f,1};
typedef struct {const uint8_t *body;unsigned calls,fail_after;} provider_t;
static bool body_row(void *context,unsigned y,uint8_t row[466]){
    provider_t *source=context;assert(source&&y<466);
    if(source->calls++==source->fail_after)return false;
    memcpy(row,source->body+y*466,466);return true;
}
static void expression(mimi_pet_t *pet,unsigned id,bool transition){
    for(unsigned eye=0;eye<2;eye++)for(unsigned i=0;i<48;i++){
        float x=mimi_expressions[id][eye][i][0]*.01f,y=mimi_expressions[id][eye][i][1]*.01f;
        if(transition){unsigned next=(id+1)%25;x+=(mimi_expressions[next][eye][i][0]*.01f-x)*.37f;y+=(mimi_expressions[next][eye][i][1]*.01f-y)*.37f;}
        pet->eyes[eye][i]=(mimi_point_t){x,y};
    }
}
static void compare(const mimi_pet_t *pet,uint32_t ms,const uint8_t *body){
    mimi_prepared_eyes_t *expected=malloc(sizeof *expected),*actual=malloc(sizeof *actual);assert(expected&&actual);
    int16_t delta[467];prepare_reference_eyes(pet,ms,body,expected);
    assert(mimi_prepare_eyes(pet,ms,body,delta,actual));
    if(memcmp(expected,actual,sizeof *actual)){
        const unsigned char *a=(const unsigned char*)actual,*e=(const unsigned char*)expected;
        for(size_t i=0;i<sizeof *actual;i++)if(a[i]!=e[i]){fprintf(stderr,"clip=%u pose=%u mismatch byte=%zu expected=%u actual=%u\n",pet->motion,ms,i,e[i],a[i]);break;}abort();
    }
    cases++;provider_t source={body,0,UINT32_MAX};memset(actual,0xa5,sizeof *actual);
    assert(mimi_prepare_eyes_rows(pet,ms,body_row,&source,delta,actual));assert(!memcmp(expected,actual,sizeof *actual));
    assert(source.calls<=416);body_reads+=source.calls;row_cases++;free(expected);free(actual);
}
static void all_clips_and_live_eyes(void){
    uint8_t *body=malloc(466*466);assert(body);
    for(unsigned clip=0;clip<14;clip++){
        mimi_pet_t pet;mimi_pet_init(&pet);mimi_pet_play(&pet,clip);
        for(uint32_t ms=0;ms<=mimi_motion_duration(clip)+450;ms+=50){
            pet.motion_ms=ms;prepare_reference_body(&pet,ms,body);clip_samples++;
            unsigned count=clip==10||clip==11||clip==13?25:1;
            for(unsigned id=0;id<count;id++){
                expression(&pet,id,false);unsigned gaze=(id+ms/50)%6;
                pet.gaze_x=gazes[gaze][0];pet.gaze_y=gazes[gaze][1];pet.lid=lids[(id+ms/50)%4];compare(&pet,ms,body);
            }
            expression(&pet,(ms/50)%25,true);pet.gaze_x=.1234567f;pet.gaze_y=-.7654321f;pet.lid=.01333333f;
            // 故意让实时钟不同于缓存姿态，防止fade权重错误沿用墙上时钟。
            // Deliberately differ the live clock from the cached pose to detect mismatched fade weights.
            pet.motion_ms=ms+17;compare(&pet,ms,body);
        }
    }
    free(body);
}
#define PARALLEL_CASES 12
static uint8_t parallel_bodies[PARALLEL_CASES][466*466];
static mimi_pet_t parallel_pets[PARALLEL_CASES];
static uint32_t parallel_ms[PARALLEL_CASES];
static mimi_prepared_eyes_t parallel_expected[PARALLEL_CASES];
static void *parallel(void *argument){
    unsigned offset=(unsigned)(uintptr_t)argument;int16_t delta[467];mimi_prepared_eyes_t *out=malloc(sizeof *out);assert(out);
    for(unsigned n=0;n<200;n++){
        unsigned i=(n+offset)%PARALLEL_CASES;
        if(n&1){provider_t source={parallel_bodies[i],0,UINT32_MAX};assert(mimi_prepare_eyes_rows(&parallel_pets[i],parallel_ms[i],body_row,&source,delta,out));}
        else assert(mimi_prepare_eyes(&parallel_pets[i],parallel_ms[i],parallel_bodies[i],delta,out));
        assert(!memcmp(out,&parallel_expected[i],sizeof *out));
    }
    free(out);return NULL;
}
static void concurrency(void){
    const unsigned clips[]={10,11,13};const unsigned times[]={0,150,600,2350};
    for(unsigned i=0;i<PARALLEL_CASES;i++){
        mimi_pet_t *pet=&parallel_pets[i];mimi_pet_init(pet);mimi_pet_play(pet,clips[i%3]);
        uint32_t ms=times[i/3];if(ms>mimi_motion_duration(pet->motion)+450)ms=0;parallel_ms[i]=ms;pet->motion_ms=ms+13;
        expression(pet,i,true);pet->gaze_x=gazes[i%6][0];pet->gaze_y=gazes[i%6][1];pet->lid=lids[i%4];
        prepare_reference_body(pet,ms,parallel_bodies[i]);prepare_reference_eyes(pet,ms,parallel_bodies[i],&parallel_expected[i]);
    }
    pthread_t a,b;assert(!pthread_create(&a,NULL,parallel,(void*)(uintptr_t)0));assert(!pthread_create(&b,NULL,parallel,(void*)(uintptr_t)7));
    assert(!pthread_join(a,NULL));assert(!pthread_join(b,NULL));
}
static void invalid_and_provider_failure(void){
    uint8_t *body=malloc(466*466);mimi_prepared_eyes_t *out=malloc(sizeof *out),*before=malloc(sizeof *before);assert(body&&out&&before);
    mimi_pet_t pet;mimi_pet_init(&pet);mimi_pet_play(&pet,11);pet.motion_ms=600;prepare_reference_body(&pet,600,body);
    int16_t delta[467];memset(out,0xa5,sizeof *out);memcpy(before,out,sizeof *out);
    assert(!mimi_prepare_eyes(NULL,600,body,delta,out));assert(!mimi_prepare_eyes(&pet,600,NULL,delta,out));
    assert(!mimi_prepare_eyes(&pet,600,body,NULL,out));assert(!mimi_prepare_eyes(&pet,600,body,delta,NULL));
    const uint32_t bad[]={1,49,51,UINT32_MAX,mimi_motion_duration(11)+500};
    for(unsigned i=0;i<sizeof bad/sizeof *bad;i++)assert(!mimi_prepare_eyes(&pet,bad[i],body,delta,out));
    pet.motion=14;assert(!mimi_prepare_eyes(&pet,600,body,delta,out));pet.motion=11;
    pet.gaze_x=NAN;assert(!mimi_prepare_eyes(&pet,600,body,delta,out));pet.gaze_x=0;
    pet.lid=INFINITY;assert(!mimi_prepare_eyes(&pet,600,body,delta,out));pet.lid=1;
    pet.eyes[1][47].y=NAN;assert(!mimi_prepare_eyes(&pet,600,body,delta,out));assert(!memcmp(out,before,sizeof *out));
    mimi_pet_init(&pet);mimi_pet_play(&pet,11);pet.motion_ms=600;
    provider_t source={body,0,0};assert(!mimi_prepare_eyes_rows(&pet,600,body_row,&source,delta,out));assert(source.calls==1);
    source=(provider_t){body,0,2};assert(!mimi_prepare_eyes_rows(&pet,600,body_row,&source,delta,out));assert(source.calls==3);
    free(body);free(out);free(before);
}
int main(void){
    all_clips_and_live_eyes();concurrency();invalid_and_provider_failure();
    printf("formula_full_mask_cases=%u row_api_cases=%u clips=14 clip_samples=%u expressions=25 body_row_reads=%u concurrent_mixed_calls=400 invalid_inputs=pass provider_failure=pass byte_exact=true\n",cases,row_cases,clip_samples,body_reads);return 0;
}
