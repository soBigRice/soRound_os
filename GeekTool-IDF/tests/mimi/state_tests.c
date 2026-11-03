#include "mimi_motion.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void advance(mimi_pet_t *pet,unsigned ms) {
    for(unsigned elapsed=0;elapsed<ms;elapsed+=33)mimi_pet_step(pet,33);
}
int main(void) {
    mimi_pet_t pet;mimi_pet_init(&pet);bool blink=false;
    for(unsigned i=0;i<1000;++i) { mimi_pet_step(&pet,33);blink|=pet.lid<.2f; }assert(blink);
    mimi_pet_init(&pet);
    for(unsigned i=0;i<25;++i) {
        mimi_point_t previous=pet.eyes[0][0];mimi_pet_react(&pet,false);
        assert(!memcmp(&previous,&pet.eyes[0][0],sizeof previous));advance(&pet,500);
    }
    assert(pet.expression_seen==0x1ffffff);
    mimi_pet_init(&pet);uint32_t visibly_held=0;
    for(unsigned elapsed=0;elapsed<160000;elapsed+=33) {
        mimi_pet_step(&pet,33);
        if(!pet.motion_active&&pet.expression_ms>=1000)visibly_held|=1u<<pet.expression;
    }
    // 必须证明自动调度实际覆盖全库，不能只验证数组里存在资源。
    // Prove actual autonomous playback reaches every item, not merely that assets exist.
    assert(pet.expression_seen==0x1ffffff&&visibly_held==0x1ffffff&&pet.motion_seen==0x3fff);
    advance(&pet,21000);assert(pet.mood==MIMI_DROWSY);
    advance(&pet,30000);assert(pet.mood==MIMI_SLEEPING);
    mimi_pet_touch(&pet,1,-1,true);assert(pet.mood==MIMI_SURPRISED&&!pet.quiet_ms);
    advance(&pet,3000);assert(!pet.quiet_ms);
    mimi_pet_init(&pet);mimi_pet_react(&pet,true);assert(pet.motion==MIMI_MOTION_THINKING);
    advance(&pet,600);unsigned active=pet.motion;uint32_t clock=pet.motion_ms;
    mimi_pet_play(&pet,MIMI_MOTION_ORBIT);assert(pet.motion==active&&pet.motion_ms==clock&&pet.motion_queued);
    advance(&pet,mimi_motion_duration(active));assert(pet.motion==MIMI_MOTION_ORBIT);
    mimi_motion_pose_t pose;
    for(unsigned id=0;id<14;++id) {
        assert(mimi_motion_duration(id)>=1500&&strcmp(mimi_motion_name(id),"invalid"));
        for(unsigned ms=0;ms<=mimi_motion_duration(id);ms+=33) {
            mimi_motion_sample(id,ms,&pose);
            for(unsigned i=0;i<64;++i)assert(isfinite(pose.body[i].x)&&isfinite(pose.body[i].y)&&fabsf(pose.body[i].x)<2&&fabsf(pose.body[i].y)<2);
            for(unsigned eye=0;eye<2;++eye)assert(pose.eye_opa[eye]>=0&&pose.eye_opa[eye]<=1);
        }
    }
    mimi_pet_touch(&pet,INFINITY,NAN,true);assert(pet.target_x==0&&pet.target_y==0);
    mimi_pet_touch(&pet,100,-100,true);assert(pet.target_x==1&&pet.target_y==-1);
    pet.clock=UINT32_MAX-10;mimi_pet_step(&pet,UINT32_MAX);assert(pet.clock==69);
    puts("Mimi: all 25 expressions and 14 motions reached by real scheduling; bounded morph/queue, blink, sleep/wake and invalid input passed");
}
