#include "../lifestyle/native_ui.h"
#include "esp_heap_caps.h"
#include <stdlib.h>

static unsigned allocations,back_requests;
static bool fail_allocation,fullscreen;
void go_home(void) { ++back_requests; }
void launcher_set_app_fullscreen(bool enabled) {
    fullscreen=enabled;
    for(unsigned i=0;i<lv_obj_get_child_count(lv_layer_top());++i)ui_obj_set_hidden(lv_obj_get_child(lv_layer_top(),i),enabled);
}
static void *test_allocate(size_t bytes,unsigned caps) {
    assert(bytes==308840&&caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(fail_allocation)return NULL;
    void *memory=malloc(bytes);assert(memory);++allocations;return memory;
}
static void test_free(void *memory) { if(memory){assert(allocations);--allocations;free(memory);} }
#define heap_caps_malloc test_allocate
#define heap_caps_free test_free
#include "../../main/app_mimi.c"
#undef heap_caps_malloc
#undef heap_caps_free

// 对照原四采样公式，防止性能改动损伤凹轮廓、裁切或边缘覆盖率。
// Compare against the original four-sample formula to protect concave contours, clipping and edge coverage.
static void reference_raster(uint8_t *pixels, int width, int height, const mimi_point_t *ring, unsigned count) {
    memset(pixels,0,(size_t)width*height);
    float min_y=(float)height,max_y=0;
    for(unsigned i=0;i<count;++i) { min_y=fminf(min_y,ring[i].y); max_y=fmaxf(max_y,ring[i].y); }
    int first=(int)clamp(floorf(min_y),0,(float)height),last=(int)clamp(ceilf(max_y),0,(float)height);
    for(int row=first;row<last;++row) for(unsigned sample=0;sample<4;++sample) {
        float y=row+(sample+.5f)*.25f,hits[64];unsigned used=0;
        for(unsigned i=0,j=count-1;i<count;j=i++) {
            mimi_point_t a=ring[j],b=ring[i];
            if((a.y>y)==(b.y>y))continue;
            float x=a.x+(y-a.y)*(b.x-a.x)/(b.y-a.y);unsigned at=used++;
            while(at&&hits[at-1]>x) { hits[at]=hits[at-1];--at; } hits[at]=x;
        }
        for(unsigned i=0;i+1<used;i+=2) {
            float left=fmaxf(0,hits[i]),right=fminf((float)width,hits[i+1]);
            for(int x=(int)floorf(left);x<(int)ceilf(right);++x) {
                if(x<0||x>=width)continue;
                float area=fmaxf(0,fminf(x+1.f,right)-fmaxf((float)x,left));
                unsigned alpha=pixels[row*width+x]+(unsigned)lroundf(area*63.75f);
                pixels[row*width+x]=alpha>255?255:(uint8_t)alpha;
            }
        }
    }
}

static void check_raster_equivalence(void) {
    uint8_t *expected=malloc(BODY_BYTES),*actual=malloc(BODY_BYTES);assert(expected&&actual);
    for(unsigned clip=0;clip<MIMI_MOTION_COUNT;++clip) for(unsigned phase=0;phase<9;++phase) {
        mimi_motion_pose_t pose;mimi_motion_sample(clip,mimi_motion_duration(clip)*phase/8,&pose);
        mimi_point_t ring[64];
        for(unsigned i=0;i<64;++i)ring[i]=(mimi_point_t){232.5f+pose.body[i].x*220,232.5f+pose.body[i].y*220};
        reference_raster(expected,SCREEN,SCREEN,ring,64);raster(actual,SCREEN,SCREEN,ring,64);
        assert(!memcmp(expected,actual,BODY_BYTES));
        for(unsigned eye=0;eye<2;++eye) {
            for(unsigned i=0;i<48;++i)ring[i]=(mimi_point_t){104.25f+pose.eyes[eye][i].x*220,103.75f+pose.eyes[eye][i].y*220};
            reference_raster(expected,EYE_SIZE,EYE_SIZE,ring,48);raster(actual,EYE_SIZE,EYE_SIZE,ring,48);
            assert(!memcmp(expected,actual,EYE_SIZE*EYE_SIZE));
        }
    }
    // 星形凹轮廓与屏幕外部分覆盖也必须保持逐像素一致。
    // Concave stars and partially off-screen contours must remain pixel-identical as well.
    for(unsigned offset=0;offset<5;++offset) {
        mimi_point_t ring[64];
        for(unsigned i=0;i<64;++i){float angle=i*TAU/64,r=i%2?210:90;ring[i]=(mimi_point_t){offset*100.f-30+r*cosf(angle),offset*100.f-30+r*sinf(angle)};}
        reference_raster(expected,SCREEN,SCREEN,ring,64);raster(actual,SCREEN,SCREEN,ring,64);
        assert(!memcmp(expected,actual,BODY_BYTES));
    }
    free(expected);free(actual);
}

static lv_indev_data_t pointer={.state=LV_INDEV_STATE_RELEASED};
static void read_pointer(lv_indev_t *input,lv_indev_data_t *data) { (void)input;*data=pointer; }
static void move_pointer(lv_indev_t *input,int x,int y,bool down) {
    pointer.point=(lv_point_t){x,y};pointer.state=down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;lv_indev_read(input);
}
static void frames(unsigned ms) { for(unsigned elapsed=0;elapsed<ms;elapsed+=33){lv_tick_inc(33);app_mimi.tick();} }
int main(int argc,char **argv) {
    check_raster_equivalence();
    const char *dir=argc>1?argv[1]:NULL;bool animation=argc>2&&!strcmp(argv[2],"--animation");
    for(language=0;language<2;++language) {
        back_requests=0;init();lv_obj_t *p=page("Mimi");
        lv_indev_t *input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(input,display);lv_indev_set_read_cb(input,read_pointer);
        app_mimi.enter(p);assert(masks&&allocations==1&&fullscreen);
        capture(dir,"mimi-fullscreen");
        // 边缘允许抗锯齿半覆盖；四个方向都触边，内侧一像素必须实心。
        // Boundary pixels may have partial AA coverage; all four edges are reached and the interior is opaque.
        assert(masks[233]&&masks[465*466+233]&&masks[233*466]&&masks[233*466+465]);
        assert(masks[466+233]>230&&masks[464*466+233]>230&&masks[233*466+1]>230&&masks[233*466+464]>230);
        assert(ui_obj_is_hidden(return_button));tap(root);assert(pet.expression==0);
        move_pointer(input,190,260,true);frames(66);assert(pressed&&pet.touching);
        move_pointer(input,280,250,true);frames(66);assert(pet.target_x>0&&stroke_distance>=24);
        move_pointer(input,280,250,false);frames(650);assert(pet.motion_active);
        capture(dir,"mimi-thinking");
        // 长按揭示返回，但松手不得额外切换表情或意外退出。
        // Holding reveals back; releasing neither changes the expression nor exits by accident.
        move_pointer(input,233,300,true);frames(1000);assert(!ui_obj_is_hidden(return_button)&&consumed);
        unsigned expression=pet.expression;move_pointer(input,233,300,false);assert(pet.expression==expression&&!back_requests);
        capture(dir,"mimi-return");circle(return_button);tap(return_button);assert(back_requests);frames(3600);assert(ui_obj_is_hidden(return_button));
        for(unsigned motion=0;motion<14;++motion) {
            mimi_pet_init(&pet);mimi_pet_play(&pet,motion);
            frames(1000);char name[64];snprintf(name,sizeof name,"motion-%02u-%s",motion,mimi_motion_name(motion));capture(dir,name);
        }
        for(unsigned expression=0;expression<25;++expression) {
            mimi_pet_init(&pet);
            for(unsigned i=0;i<expression;++i)mimi_pet_react(&pet,false);
            frames(1200);assert(pet.expression==expression);
            char name[64];snprintf(name,sizeof name,"expression-%02u",expression);capture(dir,name);
        }
        if(animation&&language) {
            const unsigned showcase[]={MIMI_MOTION_THINKING,MIMI_MOTION_EXCLAIM,MIMI_MOTION_EGG,MIMI_MOTION_PLAY,MIMI_MOTION_ORBIT,MIMI_MOTION_BURST,MIMI_MOTION_COMET};
            for(unsigned clip=0;clip<sizeof showcase/sizeof showcase[0];++clip) {
                mimi_pet_init(&pet);mimi_pet_play(&pet,showcase[clip]);
                unsigned count=(mimi_motion_duration(showcase[clip])+450)/66;
                for(unsigned i=0;i<count;++i) {
                    frames(66);char name[64];snprintf(name,sizeof name,"frame-%02u-%03u",clip,i);capture(dir,name);
                }
            }
        }
        app_mimi.visibility(false);mimi_pet_t paused=pet;lv_refr_now(display);unsigned before=flushed;
        lv_tick_inc(90000);for(unsigned i=0;i<100;++i)app_mimi.tick();lv_refr_now(display);
        assert(!memcmp(&paused,&pet,sizeof pet)&&flushed==before);
        app_mimi.visibility(true);app_mimi.tick();assert(!memcmp(&paused,&pet,sizeof pet));
        lv_tick_inc(33);app_mimi.tick();assert(pet.clock==paused.clock+33);
        app_mimi.exit();assert(!masks&&!root&&!allocations&&!fullscreen);lv_obj_clean(p);
        for(unsigned i=0;i<12;++i){app_mimi.enter(p);frames(99);lv_obj_clean(p);assert(!allocations&&!root&&!fullscreen);}
        fail_allocation=true;app_mimi.enter(p);assert(!masks&&!allocations&&error_label&&!ui_obj_is_hidden(return_button));
        capture(dir,"mimi-no-memory");app_mimi.tick();app_mimi.exit();lv_obj_clean(p);fail_allocation=false;
        lv_indev_delete(input);lv_deinit();
    }
    puts("Mimi: full viewport, 14 rendered clips, native touch/hold/back, hidden freeze/no-flush, OOM and repeated cleanup passed");
}
