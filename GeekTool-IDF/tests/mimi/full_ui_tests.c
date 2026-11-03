#include "../../tools/mimi_assets/native_ui.h"
#include "esp_heap_caps.h"
#include "mimi_pipeline.h"
#include "mimi_render.h"
#include "test_hooks.h"
#include "freertos/task.h"
#include "src/draw/lv_draw_private.h"
#include "src/draw/sw/lv_draw_sw.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <zlib.h>
#include "mimi_expressions.inc"
#include "full_expected.inc"

static const char *test_asset_path;
static bool mount_ok=true;
static unsigned allocations,allocation_mode,previous_calls;
static atomic_uint asset_blocks,asset_attempts,asset_fail;
void *full_asset_malloc(size_t bytes){
    unsigned attempt=atomic_fetch_add(&asset_attempts,1)+1;if(attempt==atomic_load(&asset_fail))return NULL;
    void *p=malloc(bytes);if(p)atomic_fetch_add(&asset_blocks,1);return p;
}
void full_asset_free(void *memory){if(memory){assert(atomic_fetch_sub(&asset_blocks,1)>0);free(memory);}}
bool resource_store_mount(void){return mount_ok;}
void go_home(void){}
void launcher_set_app_fullscreen(bool enabled){
    for(unsigned i=0;i<lv_obj_get_child_count(lv_layer_top());i++)ui_obj_set_hidden(lv_obj_get_child(lv_layer_top(),i),enabled);
}
static void *ui_allocate(size_t bytes,unsigned caps){
    assert((bytes==308840||bytes==mimi_render_bytes())&&caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if((allocation_mode==1&&bytes==308840)||(allocation_mode==2&&bytes==mimi_render_bytes()))return NULL;
    void *p=malloc(bytes);assert(p);allocations++;return p;
}
static void ui_free(void *p){if(p){assert(allocations);allocations--;free(p);}}
#undef MIMI_ASSET_PATH
#define MIMI_ASSET_PATH test_asset_path
#define heap_caps_malloc ui_allocate
#define heap_caps_free ui_free
#include "../../main/app_mimi.c"
#undef heap_caps_malloc
#undef heap_caps_free

static lv_color_format_t format;
static uint16_t reference[466*466],frozen[466*466],overlay[466*466];
static uint8_t raw_pixels[466*466*2];
static unsigned native_comparisons,dynamic_comparisons,fallback_paths,early_poll_checks,ordinary_gate_checks;
static uint32_t pixels_crc(const void *data,size_t bytes){return (uint32_t)crc32(0,data,(uInt)bytes);}
static void normalize_flush(lv_display_t *d,const lv_area_t *a,uint8_t *map){
    int width=lv_area_get_width(a);flushed+=(unsigned)(width*lv_area_get_height(a));
    for(int y=a->y1;y<=a->y2;y++){
        memcpy(raw_pixels+(y*466+a->x1)*2,map,(size_t)width*2);
        if(format==LV_COLOR_FORMAT_RGB565_SWAPPED){for(int x=0;x<width;x++)pixels[y*466+a->x1+x]=(uint16_t)(map[x*2]<<8|map[x*2+1]);}
        else memcpy(pixels+y*466+a->x1,map,(size_t)width*2);
        map+=width*2;
    }
    lv_display_flush_ready(d);
}
static void refresh(void){lv_obj_invalidate(lv_screen_active());lv_refr_now(display);}
static void buffer_rows(unsigned rows){lv_display_set_buffers(display,buffer,NULL,466*rows*2,LV_DISPLAY_RENDER_MODE_PARTIAL);}
static void compare(const uint16_t *actual,const uint16_t *expected,const char *label,unsigned number){
    if(!memcmp(actual,expected,sizeof pixels))return;
    unsigned count=0;for(unsigned i=0;i<466*466;i++)if(actual[i]!=expected[i]){
        if(count<8)fprintf(stderr,"%s case%u cf%u xy%u,%u actual%04x expected%04x\n",label,number,(unsigned)format,i%466,i/466,actual[i],expected[i]);count++;
    }
    fprintf(stderr,"%s mismatches=%u actualCRC=%u expectedCRC=%u\n",label,count,pixels_crc(actual,sizeof pixels),pixels_crc(expected,sizeof pixels));abort();
}
static mimi_pet_t input(unsigned clip,unsigned ms){mimi_pet_t p;mimi_pet_init(&p);mimi_pet_play(&p,clip);p.motion_ms=ms;return p;}
static bool matches(const mimi_pipeline_frame_t *frame,const mimi_pet_t *wanted){
    if(!frame||frame->asset.frame.clip!=wanted->motion||frame->asset.frame.pose_ms!=wanted->motion_ms/50*50)return false;
    const mimi_pet_t *p=&frame->snapshot;
    return p->gaze_x==wanted->gaze_x&&p->gaze_y==wanted->gaze_y&&p->lid==wanted->lid&&
           !memcmp(p->eyes,wanted->eyes,sizeof p->eyes);
}
static void prepared(const mimi_pet_t *wanted){
    assert(accel_pipeline&&accel_renderer&&!accel_disabled);pet=*wanted;TickType_t began=xTaskGetTickCount();
    for(;;){
        render_pet();assert(!accel_disabled);
        if(accel_show&&matches(accel_published,wanted)){refresh();return;}
        assert(xTaskGetTickCount()-began<5000);vTaskDelay(1);
    }
}
static void full_native_matrix(void){
    for(unsigned i=0;i<186;i++){
        mimi_pet_t p=input(expected_clip[i],expected_ms[i]);prepared(&p);
        uint32_t actual=pixels_crc(pixels,sizeof pixels),raw=pixels_crc(raw_pixels,sizeof raw_pixels);
        if(actual!=expected_native[i]||raw!=(format==LV_COLOR_FORMAT_RGB565_SWAPPED?expected_swapped[i]:expected_native[i])){
            fprintf(stderr,"native case%u clip%u ms%u cf%u actual=%u expected=%u raw=%u\n",i,expected_clip[i],expected_ms[i],(unsigned)format,actual,expected_native[i],raw);abort();
        }
        native_comparisons++;
    }
}
static void vector_reference(const mimi_pet_t *wanted,unsigned rows,uint16_t *out){
    accel_disabled=true;pet=*wanted;buffer_rows(rows);render_pet();refresh();memcpy(out,pixels,sizeof pixels);
    accel_disabled=false;buffer_rows(40);
}
static void dynamic_eyes_and_fades(void){
    const unsigned clips[]={10,11,13};
    for(unsigned c=0;c<3;c++)for(unsigned sample=0;sample<3;sample++)for(unsigned id=0;id<25;id++){
        unsigned ms=sample==0?150:sample==1?650:mimi_motion_duration(clips[c])+200;
        mimi_pet_t p=input(clips[c],ms);
        for(unsigned e=0;e<2;e++)for(unsigned v=0;v<48;v++)p.eyes[e][v]=(mimi_point_t){mimi_expressions[id][e][v][0]*.01f,mimi_expressions[id][e][v][1]*.01f};
        p.gaze_x=id&1?-1:.1234567f;p.gaze_y=id&2?1:-.7654321f;p.lid=id%3==0?0:id%3==1?.5f:1;
        vector_reference(&p,466,reference);prepared(&p);compare(pixels,reference,"dynamic eyes/fade",c*75+sample*25+id);dynamic_comparisons++;
    }
}
static void await_done_count(unsigned count){
    TickType_t started=xTaskGetTickCount();
    while(test_done_overwrites()<count){assert(xTaskGetTickCount()-started<5000);vTaskDelay(1);}
}
static void early_publication_keeps_original_clock(void){
    assert(app_mimi.tick_period_ms==20);mimi_pet_t p=input(11,600);prepared(&p);
    uint32_t old_serial=accel_published->serial;last_tick=lv_tick_get();pet.motion_ms=650;p=pet;
    unsigned done=test_done_overwrites();render_pet();await_done_count(done+1);
    pressed=true;consumed=false;stroke_distance=0;pressed_at=lv_tick_get()-900;
    mimi_pipeline_stats_t before,after;mimi_pipeline_stats(accel_pipeline,&before);uint32_t original_tick=last_tick;
    lv_tick_inc(20);app_mimi.tick();
    assert(!memcmp(&pet,&p,sizeof pet)&&last_tick==original_tick&&!consumed);
    assert(accel_published->serial>old_serial&&accel_published->asset.frame.pose_ms==650);lv_refr_now(display);
    mimi_pipeline_stats(accel_pipeline,&after);assert(after.presented==before.presented+1);unsigned flushed_before=flushed;
    for(unsigned i=0;i<100;i++){
        app_mimi.tick();lv_refr_now(display);assert(!memcmp(&pet,&p,sizeof pet)&&last_tick==original_tick&&!consumed);early_poll_checks++;
    }
    mimi_pipeline_stats(accel_pipeline,&before);assert(before.presented==after.presented&&flushed==flushed_before);
    mimi_pet_t expected=pet;mimi_pet_step(&expected,40);lv_tick_inc(20);app_mimi.tick();
    assert(!memcmp(&pet,&expected,sizeof pet)&&last_tick==original_tick+40&&consumed&&!ui_obj_is_hidden(return_button));
    pressed=consumed=false;ui_obj_set_hidden(return_button,true);
    p=input(4,500);pet=p;render_pet();refresh();assert(!accel_requesting&&!accel_show);last_tick=lv_tick_get();flushed_before=flushed;
    lv_tick_inc(20);app_mimi.tick();lv_refr_now(display);assert(!memcmp(&pet,&p,sizeof pet)&&flushed==flushed_before);
    expected=p;mimi_pet_step(&expected,40);lv_tick_inc(20);app_mimi.tick();assert(!memcmp(&pet,&expected,sizeof pet));ordinary_gate_checks++;
}
static void early_worker_failure_restores_vector(void){
    mimi_pet_t wanted=input(11,800);vector_reference(&wanted,40,reference);prepared(&wanted);
    unsigned done=test_done_overwrites();pet.gaze_x=NAN;render_pet();pet=wanted;await_done_count(done+1);
    last_tick=lv_tick_get();lv_tick_inc(20);app_mimi.tick();
    assert(accel_disabled&&!accel_show&&!ui_obj_is_hidden(body)&&!ui_obj_is_hidden(eyes[0])&&!ui_obj_is_hidden(decor[1]));
    assert(!memcmp(&pet,&wanted,sizeof pet));refresh();compare(pixels,reference,"early worker failure vector fallback",0);fallback_paths++;
}
static void previous_handler(lv_draw_task_t *task,const lv_draw_sw_blend_dsc_t *d){
    previous_calls++;lv_color_format_t cf=task->target_layer->color_format;lv_draw_sw_blend_handler_t current=lv_draw_sw_get_blend_handler(cf);
    lv_draw_sw_unregister_blend_handler(cf);lv_draw_sw_blend(task,d);lv_draw_sw_custom_blend_handler_t h={cf,current};assert(lv_draw_sw_register_blend_handler(&h));
}
static void overlay_and_visibility(void){
    mimi_pet_t p=input(11,950);prepared(&p);memcpy(frozen,pixels,sizeof pixels);
    app_mimi.visibility(false);mimi_pet_t paused=pet;unsigned before=flushed;
    lv_tick_inc(90000);for(unsigned i=0;i<100;i++)app_mimi.tick();lv_refr_now(display);assert(flushed==before&&!memcmp(&paused,&pet,sizeof pet));
    lv_obj_t *panel=lv_obj_create(lv_layer_top());lv_obj_remove_style_all(panel);lv_obj_set_pos(panel,101,113);lv_obj_set_size(panel,263,181);
    lv_obj_set_style_bg_color(panel,lv_color_hex(0x294574),0);lv_obj_set_style_bg_opa(panel,117,0);
    lv_obj_t *label=lv_label_create(panel);lv_label_set_text(label,"Pause");lv_obj_set_pos(label,24,30);lv_obj_set_style_text_color(label,lv_color_white(),0);
    _Alignas(4) static const uint8_t argb[16]={32,83,197,160,219,120,50,101,4,190,41,255,255,255,255,0};
    static const lv_image_dsc_t art={.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_ARGB8888,.w=2,.h=2,.stride=8},.data=argb,.data_size=16};
    lv_obj_t *icon=lv_image_create(panel);lv_image_set_src(icon,&art);lv_obj_set_pos(icon,65,66);refresh();memcpy(overlay,pixels,sizeof pixels);
    const uint8_t *old=accel_descriptor.data;mimi_render_attach(accel_renderer,NULL);lv_image_cache_drop(&accel_descriptor);accel_descriptor.data=(const uint8_t*)frozen;
    refresh();compare(pixels,overlay,"overlay vs native static image",0);
    lv_image_cache_drop(&accel_descriptor);accel_descriptor.data=old;assert(mimi_render_attach(accel_renderer,&accel_descriptor));lv_obj_delete(panel);refresh();compare(pixels,frozen,"hidden frozen image",0);
    app_mimi.visibility(true);app_mimi.tick();assert(!memcmp(&paused,&pet,sizeof pet));lv_tick_inc(33);app_mimi.tick();assert(pet.clock==paused.clock+33&&pet.motion_ms==paused.motion_ms+33);
    p=input(4,500);pet=p;render_pet();refresh();assert(!accel_show&&!ui_obj_is_hidden(body));
}
static void assert_clean(void){
    test_resources_t r=test_resources();assert(!r.cap_blocks&&!r.cap_bytes&&!r.queues&&!r.tasks&&!r.power_locks&&!r.held_power_locks);assert(!atomic_load(&asset_blocks));assert(!allocations);
}
static void exit_page(lv_obj_t *parent){app_mimi.exit();lv_obj_clean(parent);assert(!root&&!masks&&!accel_pipeline&&!accel_renderer&&!accel_image);assert_clean();}
static void lifecycle_and_fallbacks(lv_obj_t *parent,const char *pack){
    exit_page(parent);assert(lv_draw_sw_get_blend_handler(format)==NULL);
    for(unsigned cycle=0;cycle<6;cycle++){
        app_mimi.enter(parent);mimi_pet_t p=input(10+cycle%2,600);prepared(&p);
        if(cycle&1)app_mimi.exit();lv_obj_clean(parent);assert(!root&&!accel_pipeline&&!accel_renderer);assert_clean();assert(lv_draw_sw_get_blend_handler(format)==NULL);
    }
    app_mimi.enter(parent);mimi_pet_t p=input(11,600);vector_reference(&p,40,reference);exit_page(parent);
    allocation_mode=1;app_mimi.enter(parent);assert(!masks&&error_label&&!ui_obj_is_hidden(return_button));refresh();exit_page(parent);allocation_mode=0;fallback_paths++;
    allocation_mode=2;app_mimi.enter(parent);assert(masks&&!accel_renderer&&!accel_pipeline);pet=p;render_pet();refresh();compare(pixels,reference,"renderer allocation fallback",0);exit_page(parent);allocation_mode=0;fallback_paths++;
    mount_ok=false;app_mimi.enter(parent);assert(masks&&!accel_pipeline);pet=p;render_pet();refresh();compare(pixels,reference,"mount fallback",0);exit_page(parent);mount_ok=true;fallback_paths++;
    test_asset_path="/nonexistent-mimi-pack";app_mimi.enter(parent);assert(masks&&!accel_pipeline);pet=p;render_pet();refresh();compare(pixels,reference,"missing pack fallback",0);exit_page(parent);test_asset_path=pack;fallback_paths++;
    for(unsigned fail=0;fail<7;fail++){
        test_fail_allocations(fail<3?fail+1:0,fail>=3&&fail<5?fail-2:0,fail==5?1:0);if(fail==6)test_fail_power_allocation(1);
        app_mimi.enter(parent);assert(masks&&!accel_pipeline&&!accel_renderer);pet=p;render_pet();refresh();compare(pixels,reference,"backend allocation fallback",fail);exit_page(parent);fallback_paths++;
    }
    test_fail_allocations(0,0,0);
    for(unsigned fail=1;fail<=2;fail++){
        atomic_store(&asset_attempts,0);atomic_store(&asset_fail,fail);app_mimi.enter(parent);assert(masks&&!accel_pipeline);pet=p;render_pet();refresh();compare(pixels,reference,"reader allocation fallback",fail);exit_page(parent);fallback_paths++;
    }
    atomic_store(&asset_fail,0);atomic_store(&asset_attempts,0);
    lv_draw_sw_custom_blend_handler_t h={format,previous_handler};assert(lv_draw_sw_register_blend_handler(&h));
    app_mimi.enter(parent);prepared(&p);assert(previous_calls);exit_page(parent);assert(lv_draw_sw_get_blend_handler(format)==previous_handler);lv_draw_sw_unregister_blend_handler(format);
}
int main(int argc,char **argv){
    assert(argc==2);test_asset_path=argv[1];test_fail_allocations(0,0,0);
    for(unsigned swapped=0;swapped<2;swapped++){
        test_fail_allocations(0,0,0);init();format=swapped?LV_COLOR_FORMAT_RGB565_SWAPPED:LV_COLOR_FORMAT_RGB565;lv_display_set_color_format(display,format);buffer_rows(40);lv_display_set_flush_cb(display,normalize_flush);
        lv_obj_t *parent=page("Mimi");app_mimi.enter(parent);assert(masks&&accel_pipeline&&accel_renderer&&allocations==2);
        full_native_matrix();dynamic_eyes_and_fades();early_publication_keeps_original_clock();overlay_and_visibility();early_worker_failure_restores_vector();lifecycle_and_fallbacks(parent,argv[1]);lv_deinit();
        printf("format=%s native186=pass dynamic225=pass overlay_hidden_resume_reentry_fallback_handler=pass\n",swapped?"RGB565_SWAPPED":"RGB565");fflush(stdout);
    }
    printf("full_ui_native_comparisons=%u dynamic_eye_comparisons=%u fallback_paths=%u early_poll_checks=%u ordinary_gate_checks=%u exact=true\n",native_comparisons,dynamic_comparisons,fallback_paths,early_poll_checks,ordinary_gate_checks);return 0;
}
