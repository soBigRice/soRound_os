// Real app, renderer and fonts; only language and hardware random input are substituted.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "settings.h"
#include "launcher_icons.h"
#include "src/misc/lv_text_private.h"
#include "../host/tools_render.h"

static uint8_t language;
static uint32_t random_value;
static unsigned random_calls, flushed_pixels;
uint8_t settings_lang(void) { return language; }
uint32_t esp_random(void) { ++random_calls; return random_value; }
#include "../../main/app_answers.c"

_Alignas(LV_DRAW_BUF_ALIGN) static uint16_t buffer[466*48];
static uint16_t pixels[466*466];
static lv_display_t *display;
static lv_obj_t *heading;

static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *map) {
    int width=lv_area_get_width(a);
    flushed_pixels += (unsigned)(width*lv_area_get_height(a));
    for(int y=a->y1;y<=a->y2;++y) {
        memcpy(pixels+y*466+a->x1,map,(size_t)width*2);map+=width*2;
    }
    lv_display_flush_ready(d);
}
static void check_circle(lv_obj_t *obj) {
    lv_area_t a;lv_obj_get_coords(obj,&a);
    lv_obj_get_transformed_area(obj,&a,LV_OBJ_POINT_TRANSFORM_FLAG_RECURSIVE);
    for(int i=0;i<4;++i) {
        int x=i&1?a.x2:a.x1,y=i&2?a.y2:a.y1;
        if(hypot(x-232.5,y-232.5)>228) {
            fprintf(stderr,"outside circle: %s (%d,%d)-(%d,%d)\n",
                    lv_obj_check_type(obj,&lv_label_class)?lv_label_get_text(obj):"control",a.x1,a.y1,a.x2,a.y2);
        }
        assert(hypot(x-232.5,y-232.5)<=228);
    }
}
static void check_labels(lv_obj_t *obj) {
    if(ui_obj_is_hidden(obj))return;
    if(lv_obj_check_type(obj,&lv_label_class)) {
        const char *value=lv_label_get_text(obj);const lv_font_t *font=lv_obj_get_style_text_font(obj,0);
        uint32_t at=0;
        while(value[at]) {
            uint32_t code=lv_text_encoded_next(value,&at);if(code=='\n')continue;
            lv_font_glyph_dsc_t glyph;
            assert(lv_font_get_glyph_dsc(font,&glyph,code,0)&&!glyph.is_placeholder);
        }
        if(*value)check_circle(obj);
    }
    for(uint32_t i=0;i<lv_obj_get_child_count(obj);++i)check_labels(lv_obj_get_child(obj,i));
}
static void capture(const char *dir,const char *name) {
    lv_obj_update_layout(lv_screen_active());lv_obj_update_layout(lv_layer_top());
    check_labels(lv_screen_active());check_labels(lv_layer_top());check_circle(g_action);
    lv_refr_now(display);
    if(!dir)return;
    char path[1024];snprintf(path,sizeof path,"%s/answers-%s-%s.ppm",dir,name,language?"zh":"en");
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n466 466\n255\n");
    for(unsigned i=0;i<466*466;++i) {
        uint16_t p=pixels[i];uint8_t rgb[]={((p>>11)&31)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};
        fwrite(rgb,1,3,f);
    }
    assert(fclose(f)==0);
}
static void advance(unsigned ms) {lv_tick_inc(ms);app_answers.tick();}
static void tap(lv_obj_t *obj) {lv_obj_send_event(obj,LV_EVENT_CLICKED,NULL);}
static lv_obj_t *enter(void) {
    lv_obj_t *page=lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(page);lv_obj_set_size(page,466,466);
    lv_label_set_text(heading,tr_app_name(app_answers.name));
    app_answers.enter(page);return page;
}
int main(int argc,char **argv) {
    const char *dir=argc>1?argv[1]:NULL;
    assert(ANSWER_COUNT==48);assert(app_answers.tick_period_ms==20&&!app_answers.tick_in_background);
    lv_init();i18n_init();display=lv_display_create(466,466);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,buffer,NULL,sizeof buffer,LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);lv_obj_set_style_bg_color(lv_screen_active(),lv_color_black(),0);
    heading=lv_label_create(lv_layer_top());lv_obj_set_style_text_font(heading,&font_location_24,0);
    lv_obj_set_style_text_color(heading,lv_color_hex(COL_TXT),0);lv_obj_set_width(heading,166);
    lv_obj_set_style_text_align(heading,LV_TEXT_ALIGN_CENTER,0);lv_obj_align(heading,LV_ALIGN_TOP_MID,0,52);
    tools_test_battery();
    lv_obj_t *back=lv_obj_create(lv_layer_top()),*arrow=lv_label_create(back);
    lv_obj_set_size(back,44,44);lv_obj_align(back,LV_ALIGN_TOP_MID,-110,52);
    lv_obj_set_style_radius(back,LV_RADIUS_CIRCLE,0);lv_obj_set_style_pad_all(back,0,0);
    lv_obj_set_style_bg_color(back,lv_color_hex(0x16161a),0);
    lv_obj_set_style_border_width(back,1,0);lv_obj_set_style_border_color(back,lv_color_hex(COL_TXT2),0);
    lv_obj_set_style_text_font(arrow,UI_FONT_SYM,0);lv_label_set_text(arrow,LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(arrow,lv_color_hex(COL_TXT),0);lv_obj_center(arrow);
    for(language=0;language<2;++language) {
        lv_obj_t *page=enter();capture(dir,"cover");
        assert(!s_turning&&s_last==-1&&s_pages==0);
        for(unsigned i=0;i<ANSWER_COUNT;++i)for(unsigned j=i+1;j<ANSWER_COUNT;++j) {
            assert(strcmp(ANSWERS[i].en,ANSWERS[j].en)&&strcmp(ANSWERS[i].zh,ANSWERS[j].zh));
        }
        random_value=0;unsigned calls=random_calls;
        tap(g_cover);assert(s_turning&&random_calls==calls+1);
        tap(g_action);tap(g_cover);assert(random_calls==calls+1);
        advance(180);assert(lv_obj_get_style_transform_scale_x(g_cover,0)<256);
        capture(dir,"opening");
        // Lock/quick-panel coverage freezes exactly this phase, including taps.
        uint32_t elapsed=s_elapsed;
        app_answers.visibility(false);lv_tick_inc(4000);tap(g_action);app_answers.tick();
        assert(s_elapsed==elapsed&&random_calls==calls+1);
        app_answers.visibility(true);advance(20);assert(s_elapsed==elapsed+20);
        advance(160);capture(dir,"revealing");
        advance(360);assert(!s_turning&&s_last==0&&s_pages==1);capture(dir,"first");
        assert(!strcmp(lv_label_get_text(g_answer),language?ANSWERS[0].zh:ANSWERS[0].en));
        assert(!lv_obj_has_state(g_action,LV_STATE_DISABLED));
        assert(!strcmp(lv_label_get_text(g_page),"PAGE 01"));
        // Every remaining response is reached through the actual selection branch; no adjacent repeat.
        for(unsigned index=1;index<ANSWER_COUNT;++index) {
            random_value=index-1;calls=random_calls;tap(g_reading);
            assert(s_pending==index&&random_calls==calls+1);tap(g_action);assert(random_calls==calls+1);
            advance(180);capture(NULL,"turning");advance(180);capture(NULL,"midpoint");
            advance(360);assert(!s_turning&&s_last==(int)index&&s_pages==index+1);
            char name[32];snprintf(name,sizeof name,"page-%02u",index+1);capture(dir,name);
            assert(!strcmp(lv_label_get_text(g_answer),language?ANSWERS[index].zh:ANSWERS[index].en));
        }
        random_value=UINT32_MAX;tap(g_action);assert(s_pending<ANSWER_COUNT&&s_pending!=(unsigned)s_last);
        advance(UINT32_MAX-10);assert(!s_turning);capture(NULL,"delayed");
        // Page numbers remain bounded, idle pages trigger no repaint, exit cancels without callbacks.
        s_pages=9999;tap(g_action);advance(720);assert(s_pages==1);
        capture(NULL,"idle");flushed_pixels=0;
        for(int i=0;i<20;++i)advance(20);
        lv_refr_now(display);assert(flushed_pixels==0);
        tap(g_action);advance(160);app_answers.exit();lv_obj_delete(page);advance(1000);
        assert(!g_cover&&!s_turning);
        page=enter();capture(NULL,"reentry");assert(s_last==-1&&!s_turning&&s_pages==0);
        app_answers.exit();lv_obj_delete(page);
    }
    lv_deinit();
    puts("answers: 48 original bilingual responses, glyph/circle coverage, cover and page turns, repeated-tap guard, no adjacent repeat, cover/resume, clock wrap/delays, counter bound, idle no-redraw and exit/reentry passed");
    return 0;
}
