// 用真实 LVGL、OTA 页面、文案和字库检查布局及按钮状态,替换联网和任务创建。
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "ota_update.h"
#include "sdk.h"
static uint8_t language, beta;
static int saves;
static int tasks, create_result=pdPASS;
static bool wifi_connected=true;
static const char *task_url;
uint8_t settings_lang(void) { return language; }
uint8_t settings_beta(void) { return beta; }
void settings_set_beta(uint8_t v) { beta=v; }
void settings_save(void) { saves++; }
static lv_obj_t *heading;
void launcher_set_title(const char *text) { lv_label_set_text(heading,text); }
static const esp_app_desc_t current={ .version="v1.7-beta.10" };
const esp_app_desc_t *esp_app_get_description(void) { return &current; }
int xTaskCreate(void (*task)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle) {
    (void)task; (void)name; (void)stack; (void)priority; (void)handle;
    tasks++; task_url=arg; return create_result;
}
void vTaskDelay(int ms) { (void)ms; }
void vTaskDelete(void *task) { (void)task; }
void esp_restart(void) { assert(0); }
esp_err_t esp_wifi_get_ps(wifi_ps_type_t *ps) { *ps=WIFI_PS_MIN_MODEM; return ESP_OK; }
esp_err_t esp_wifi_set_ps(wifi_ps_type_t ps) { (void)ps; return ESP_OK; }
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap) { (void)ap; return wifi_connected?ESP_OK:ESP_FAIL; }
ota_status_t ota_update_run(const char *url,ota_status_cb cb,void *user) {
    (void)url; (void)cb; (void)user; assert(0); return (ota_status_t){0};
}
#include "../../main/app_ota.c"

#define W 466
// Native uint16_t arrays guarantee only 2-byte alignment; LVGL's draw buffer requires 4.
_Alignas(LV_DRAW_BUF_ALIGN) static uint16_t buffer[W*W];
static uint16_t pixels[W*W];
static lv_indev_t *input;
static lv_point_t pointer;
static lv_indev_state_t pointer_state;
static void read_pointer(lv_indev_t *device,lv_indev_data_t *data) {
    (void)device; data->point=pointer; data->state=pointer_state;
}
static void click(lv_obj_t *obj) {
    lv_obj_update_layout(lv_screen_active());
    lv_area_t a; lv_obj_get_coords(obj,&a);
    pointer=(lv_point_t){(a.x1+a.x2)/2,(a.y1+a.y2)/2};
    pointer_state=LV_INDEV_STATE_PRESSED; lv_tick_inc(20); lv_indev_read(input);
    pointer_state=LV_INDEV_STATE_RELEASED; lv_tick_inc(20); lv_indev_read(input);
}
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *map) {
    int width=lv_area_get_width(area);
    for (int y=area->y1;y<=area->y2;y++) { memcpy(pixels+y*W+area->x1,map,width*2); map+=width*2; }
    lv_display_flush_ready(display);
}
static void capture(lv_obj_t *page,const char *name) {
    lv_obj_update_layout(page); lv_obj_invalidate(page); lv_tick_inc(480); lv_timer_handler();
    lv_refr_now(lv_display_get_default()); // 每个状态都完成真实绘制,不能捕获上一帧
    // Export only on request; ordinary regressions must not leave PPM caches.
    const char *directory=getenv("OTA_CAPTURE_DIR");
    if (!directory || !directory[0]) return;
    char path[1024];
    int n=snprintf(path,sizeof path,"%s/wxesp32-ota-%s-%s.ppm",directory,name,language?"zh":"en");
    assert(n>0 && n<(int)sizeof path);
    FILE *f=fopen(path,"wb"); assert(f); fprintf(f,"P6\n466 466\n255\n");
    for (int i=0;i<W*W;i++) {
        unsigned p=pixels[i]; unsigned char rgb[]={ ((p>>11)&31)*255/31,((p>>5)&63)*255/63,(p&31)*255/31 };
        assert(fwrite(rgb,1,3,f)==3);
    }
    fclose(f);
}
static int colored_dots(uint32_t color) {
    int count=0;
    for(int i=0;i<ORBIT_N;i++) if(s_orbit_colors[i]==color) count++;
    return count;
}
static void assert_round_screen(void) {
    for(int y=0;y<W;y++) for(int x=0;x<W;x++) {
        if((x-232.5)*(x-232.5)+(y-232.5)*(y-232.5)>232.5*232.5 && pixels[y*W+x]) {
            fprintf(stderr,"visible pixel outside round display at (%d,%d)\n",x,y);
            assert(0);
        }
    }
}
int main(void) {
    lv_init();
    lv_display_t *display=lv_display_create(W,W);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,buffer,NULL,sizeof buffer,LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush); i18n_init();
    lv_obj_set_style_bg_color(lv_screen_active(),lv_color_hex(COL_BG),0);
    // 重建现有 launcher 顶栏/电量层;OTA 内容来自真实页面,此图不是设备截图。
    lv_obj_t *ring=lv_arc_create(lv_layer_top()); lv_obj_set_size(ring,458,458); lv_obj_center(ring);
    lv_arc_set_rotation(ring,270); lv_arc_set_bg_angles(ring,0,360); lv_arc_set_value(ring,80);
    lv_obj_set_style_arc_color(ring,lv_color_hex(0x15151a),LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring,lv_color_hex(COL_TXT),LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(ring,8,LV_PART_MAIN); lv_obj_set_style_arc_width(ring,8,LV_PART_INDICATOR);
    lv_obj_remove_style(ring,NULL,LV_PART_KNOB); lv_obj_remove_flag(ring,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(ring,LV_OBJ_FLAG_HIDDEN); // launcher 在 OTA 页隐藏实线电量层。
    heading=lv_label_create(lv_layer_top()); lv_obj_set_style_text_font(heading,UI_FONT_L,0);
    lv_obj_set_style_text_color(heading,lv_color_hex(COL_TXT),0); lv_obj_align(heading,LV_ALIGN_TOP_MID,0,46);
    lv_obj_t *back=lv_obj_create(lv_layer_top()); lv_obj_set_size(back,48,48);
    lv_obj_set_style_radius(back,LV_RADIUS_CIRCLE,0); lv_obj_set_style_bg_opa(back,LV_OPA_TRANSP,0);
    lv_obj_set_style_border_width(back,0,0); lv_obj_set_style_border_color(back,lv_color_hex(COL_TXT2),0);
    lv_obj_set_style_border_opa(back,LV_OPA_50,0); lv_obj_set_style_pad_all(back,0,0);
    lv_obj_align(back,LV_ALIGN_TOP_MID,-100,40); lv_obj_remove_flag(back,LV_OBJ_FLAG_SCROLLABLE);
    glyph_line(back,28,17,21,24,4,1,COL_TXT); glyph_line(back,21,24,28,31,4,1,COL_TXT);
    input=lv_indev_create(); lv_indev_set_type(input,LV_INDEV_TYPE_POINTER); lv_indev_set_read_cb(input,read_pointer);
    for (language=0;language<2;language++) {
        lv_obj_t *page=lv_obj_create(lv_screen_active()); lv_obj_remove_style_all(page);
        lv_obj_set_size(page,W,W); lv_obj_set_style_bg_color(page,lv_color_hex(0),0);
        lv_obj_set_style_bg_opa(page,LV_OPA_COVER,0);
        s_task_alive=false; status_publish(&(ota_status_t){.state=OTA_IDLE},NULL); ota_enter(page);
        lv_obj_update_layout(page);
        lv_area_t title_bounds, back_bounds, gear_bounds;
        lv_obj_get_coords(heading,&title_bounds); lv_obj_get_coords(back,&back_bounds); lv_obj_get_coords(g_gear,&gear_bounds);
        assert(back_bounds.x2<title_bounds.x1 && title_bounds.x2<gear_bounds.x1);
        assert(lv_obj_has_flag(g_settings,LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_has_flag(g_progress,LV_OBJ_FLAG_HIDDEN));
        assert(!lv_obj_has_flag(g_icon,LV_OBJ_FLAG_HIDDEN));
        assert(lv_anim_get(g_icon,arrow_anim_exec));
        assert(!lv_anim_get(g_main,orbit_anim_exec));
        // A bounded object count protects the internal heap used by TLS.
        assert(lv_obj_get_child_count(g_icon)==0 && lv_obj_get_child_count(g_main)<=8);
        assert(colored_dots(ORBIT_IDLE)==ORBIT_N);
        capture(page,"idle");
        assert_round_screen();
        int initial_y=lv_obj_get_y(g_icon);
        lv_tick_inc(300); lv_timer_handler(); lv_obj_update_layout(page);
        assert(lv_obj_get_y(g_icon)<initial_y);
        capture(page,"arrow-midframe");
        ota_visibility(false); assert(!lv_anim_get(g_icon,arrow_anim_exec) && !lv_anim_get(g_main,orbit_anim_exec));
        ota_visibility(true); assert(lv_anim_get(g_icon,arrow_anim_exec));
        // 真实触摸命中齿轮,设置页返回消费一次返回操作,第二次交给 launcher。
        click(g_gear); assert(s_settings_open && lv_obj_has_flag(g_main,LV_OBJ_FLAG_HIDDEN));
        assert(!lv_obj_has_flag(g_settings,LV_OBJ_FLAG_HIDDEN) && !lv_anim_get(g_icon,arrow_anim_exec));
        lv_obj_t *channel_label=lv_obj_get_child(g_channelbox,0);
        assert(lv_obj_get_x(channel_label)+lv_obj_get_width(channel_label)+4<lv_obj_get_x(g_switch));
        saves=0; uint8_t previous_beta=beta;
        click(g_switch); assert(beta!=previous_beta && saves==1);
        capture(page,"settings");
        assert(ota_back() && !ota_back());
        assert(!lv_obj_has_flag(g_main,LV_OBJ_FLAG_HIDDEN) && lv_anim_get(g_icon,arrow_anim_exec));
        ota_status_t states[]={
            {.state=OTA_CHECKING,.attempt=1},
            {.state=OTA_HEADER,.attempt=1},
            {.state=OTA_RUNNING,.attempt=1,.pct=37},
            {.state=OTA_RETRYING,.attempt=1,.pct=37},
            {.state=OTA_VERIFYING,.attempt=2,.pct=99},
            {.state=OTA_OK,.pct=100},
            {.state=OTA_UPTODATE,.version="v1.7-beta.10"},
            {.state=OTA_FAIL,.failed_at=OTA_RUNNING,.attempt=3,.error=ESP_ERR_TIMEOUT},
            {.state=OTA_FAIL,.failed_at=OTA_CHECKING,.attempt=1,.tls_flags=4,.tls_code=MBEDTLS_ERR_X509_CERT_VERIFY_FAILED},
            {.state=OTA_FAIL,.failed_at=OTA_CHECKING,.attempt=1,.tls_code=0x3000},
            {.state=OTA_FAIL,.failed_at=OTA_CHECKING,.attempt=1,.tls_code=-0x3000},
        };
        const char *names[]={"checking","header","download","retry","verify","success","uptodate","download-fail","tls-fail","tls-positive","tls-negative"};
        for (unsigned i=0;i<sizeof states/sizeof states[0];i++) {
            s_shown=(ota_state_t)-1; status_publish(&states[i],NULL); ota_tick(); lv_obj_update_layout(page);
            assert(lv_obj_get_height(g_status)<=64);
            assert(lv_obj_get_y(g_status)>ICON_Y+216);
            assert(lv_obj_get_y(g_status)+lv_obj_get_height(g_status)<455);
            bool busy=states[i].state!=OTA_FAIL && states[i].state!=OTA_UPTODATE;
            assert(lv_obj_has_state(g_switch,LV_STATE_DISABLED)==busy);
            assert(lv_obj_has_flag(g_hit,LV_OBJ_FLAG_CLICKABLE)!=busy);
            bool progress=states[i].state==OTA_RUNNING || states[i].state==OTA_RETRYING || states[i].state==OTA_VERIFYING;
            assert(lv_obj_has_flag(g_progress,LV_OBJ_FLAG_HIDDEN)!=progress);
            assert(lv_obj_has_flag(g_pctlbl,LV_OBJ_FLAG_HIDDEN)!=progress);
            assert(!lv_obj_has_flag(g_icon,LV_OBJ_FLAG_HIDDEN));
            assert((lv_anim_get(g_main,orbit_anim_exec)!=NULL)==orbit_active(states[i].state));
            if(progress) assert(lv_bar_get_value(g_progress)==states[i].pct);
            capture(page,names[i]);
            assert_round_screen();
            if(states[i].state==OTA_RUNNING) assert(colored_dots(COL_RED)==38);
            if(states[i].state==OTA_RETRYING) assert(colored_dots(OTA_AMBER)==38);
            if(states[i].state==OTA_VERIFYING) assert(colored_dots(OTA_BLUE)==102);
            if(states[i].state==OTA_OK || states[i].state==OTA_UPTODATE) assert(colored_dots(COL_CHARGE)==ORBIT_N);
            if(states[i].state==OTA_FAIL) assert(colored_dots(COL_RED)==ORBIT_N);
            if(i>=9) {
                const char *error_text=lv_label_get_text(g_status);
                assert(strstr(error_text,tr(S_OTA_TLS_FAIL)) && strstr(error_text,"TLS -0x3000"));
                assert(!strstr(error_text,"ffff"));
            }
        }
        // 环进度的四分之一/一半/全部及异常百分比;不会因为动画把未下载部分点亮。
        const int percentages[]={-3,0,25,50,75,100,120}, expected[]={0,0,26,52,78,104,104};
        for(int i=0;i<7;i++) {
            status_publish(&(ota_status_t){.state=OTA_RUNNING,.pct=percentages[i],.attempt=1},NULL); ota_tick();
            assert(colored_dots(COL_RED)==expected[i]);
        }
        status_publish(&(ota_status_t){.state=OTA_CHECKING,.attempt=1},NULL); ota_tick();
        uint32_t before=s_orbit_colors[0];
        lv_tick_inc(600); lv_timer_handler();
        assert(s_orbit_colors[0]!=before);
        // 进度持续更新但保留箭头;重连也保留已下载的百分比。
        status_publish(&(ota_status_t){.state=OTA_RUNNING,.pct=43,.attempt=1},NULL); ota_tick();
        assert(lv_bar_get_value(g_progress)==43 && strcmp(lv_label_get_text(g_pctlbl),"43%")==0);
        status_publish(&(ota_status_t){.state=OTA_RUNNING,.pct=44,.attempt=1},NULL); ota_tick();
        assert(lv_bar_get_value(g_progress)==44);
        status_publish(&(ota_status_t){.state=OTA_RETRYING,.pct=44,.attempt=1},NULL); ota_tick();
        assert(lv_bar_get_value(g_progress)==44);
        beta=1; tasks=0; click(g_gear); assert(s_settings_open);
        assert(!lv_anim_get(g_icon,arrow_anim_exec) && !lv_anim_get(g_main,orbit_anim_exec));
        start_btn(NULL); start_btn(NULL); assert(tasks==1 && strcmp(task_url,OTA_URL_BETA)==0);
        assert(lv_obj_has_state(g_switch,LV_STATE_DISABLED));
        saves=0; click(g_switch); assert(beta==1 && saves==0);
        assert(ota_back());
        // 离开页面下载继续;重入重放真实进度,默认回主界面。
        status_publish(&(ota_status_t){.state=OTA_RUNNING,.pct=61,.attempt=1},NULL);
        ota_exit(); lv_obj_delete(page);
        page=lv_obj_create(lv_screen_active()); lv_obj_remove_style_all(page); lv_obj_set_size(page,W,W);
        ota_enter(page); assert(lv_bar_get_value(g_progress)==61 && s_task_alive && !s_settings_open);
        assert(!lv_obj_has_flag(g_icon,LV_OBJ_FLAG_HIDDEN) && lv_obj_has_state(g_switch,LV_STATE_DISABLED));
        assert(colored_dots(COL_RED)==63 && lv_anim_get(g_main,orbit_anim_exec));
        ota_visibility(false); assert(!lv_anim_get(g_icon,arrow_anim_exec) && !lv_anim_get(g_main,orbit_anim_exec));
        ota_visibility(true); assert(lv_anim_get(g_icon,arrow_anim_exec) && lv_anim_get(g_main,orbit_anim_exec));
        capture(page,"reenter");
        s_task_alive=false; create_result=0; start_btn(NULL); ota_tick();
        assert(status_snapshot().error==ESP_ERR_NO_MEM && !s_task_alive);
        assert(!lv_obj_has_state(g_switch,LV_STATE_DISABLED));
        wifi_connected=false; tasks=0; start_btn(NULL);
        assert(tasks==0 && strcmp(lv_label_get_text(g_status),tr(S_CONNECT_WIFI))==0);
        wifi_connected=true;
        beta=0; create_result=pdPASS; tasks=0; click(g_hit);
        assert(tasks==1 && strcmp(task_url,OTA_URL_STABLE)==0);
        create_result=pdPASS; lv_obj_t *old_icon=g_icon,*old_main=g_main;
        ota_exit(); assert(!lv_anim_get(old_icon,arrow_anim_exec) && !lv_anim_get(old_main,orbit_anim_exec)); lv_obj_delete(page);
    }
    puts("26 bilingual round-screen renders, dotted arrow/ring animations, real progress/colors, settings and task guards passed");
    return 0;
}
