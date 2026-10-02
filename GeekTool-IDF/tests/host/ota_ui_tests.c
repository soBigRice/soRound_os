// 用真实 LVGL、OTA 页面、文案和字库检查布局及按钮状态,替换联网和任务创建。
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "ota_update.h"
#include "sdk.h"
static uint8_t language, beta;
static int tasks, create_result=pdPASS;
static const char *task_url;
uint8_t settings_lang(void) { return language; }
uint8_t settings_beta(void) { return beta; }
void settings_set_beta(uint8_t v) { beta=v; }
void settings_save(void) {}
void launcher_set_title(const char *text) { (void)text; }
static const esp_app_desc_t current={ .version="v1.7-beta.7" };
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
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap) { (void)ap; return ESP_OK; }
ota_status_t ota_update_run(const char *url,ota_status_cb cb,void *user) {
    (void)url; (void)cb; (void)user; assert(0); return (ota_status_t){0};
}
#include "../../main/app_ota.c"

#define W 466
static uint16_t buffer[W*W], pixels[W*W];
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *map) {
    int width=lv_area_get_width(area);
    for (int y=area->y1;y<=area->y2;y++) { memcpy(pixels+y*W+area->x1,map,width*2); map+=width*2; }
    lv_display_flush_ready(display);
}
static void capture(lv_obj_t *page,const char *name) {
    lv_obj_update_layout(page); lv_obj_invalidate(page); lv_tick_inc(20); lv_timer_handler();
    lv_refr_now(lv_display_get_default()); // 每个状态都完成真实绘制,不能捕获上一帧
    char path[128]; snprintf(path,sizeof path,"/tmp/wxesp32-ota-%s-%s.ppm",name,language?"zh":"en");
    FILE *f=fopen(path,"wb"); assert(f); fprintf(f,"P6\n466 466\n255\n");
    for (int i=0;i<W*W;i++) {
        unsigned p=pixels[i]; unsigned char rgb[]={ ((p>>11)&31)*255/31,((p>>5)&63)*255/63,(p&31)*255/31 };
        assert(fwrite(rgb,1,3,f)==3);
    }
    fclose(f);
}
int main(void) {
    lv_init();
    lv_display_t *display=lv_display_create(W,W);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,buffer,NULL,sizeof buffer,LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush); i18n_init();
    for (language=0;language<2;language++) {
        lv_obj_t *page=lv_obj_create(lv_screen_active()); lv_obj_remove_style_all(page);
        lv_obj_set_size(page,W,W); lv_obj_set_style_bg_color(page,lv_color_hex(0),0);
        lv_obj_set_style_bg_opa(page,LV_OPA_COVER,0);
        s_task_alive=false; status_publish(&(ota_status_t){.state=OTA_IDLE},NULL); ota_enter(page);
        lv_obj_update_layout(page);
        lv_obj_t *channel_label=lv_obj_get_child(g_channelbox,0);
        assert(lv_obj_get_x(channel_label)+lv_obj_get_width(channel_label)+4<lv_obj_get_x(g_switch));
        ota_status_t states[]={
            {.state=OTA_RETRYING,.attempt=1,.pct=37},
            {.state=OTA_VERIFYING,.attempt=2,.pct=99},
            {.state=OTA_FAIL,.failed_at=OTA_RUNNING,.attempt=3,.error=ESP_ERR_TIMEOUT},
            {.state=OTA_FAIL,.failed_at=OTA_CHECKING,.attempt=1,.tls_flags=4,.tls_code=MBEDTLS_ERR_X509_CERT_VERIFY_FAILED},
        };
        const char *names[]={"retry","verify","download-fail","tls-fail"};
        for (unsigned i=0;i<sizeof states/sizeof states[0];i++) {
            s_shown=(ota_state_t)-1; status_publish(&states[i],NULL); ota_tick(); lv_obj_update_layout(page);
            assert(lv_obj_get_height(g_status)<=64);
            assert(lv_obj_get_y(g_status)+lv_obj_get_height(g_status)<lv_obj_get_y(g_channelbox));
            bool busy=states[i].state!=OTA_FAIL;
            assert(lv_obj_has_state(g_switch,LV_STATE_DISABLED)==busy);
            assert(lv_obj_has_flag(g_hit,LV_OBJ_FLAG_CLICKABLE)!=busy);
            capture(page,names[i]);
        }
        beta=1; tasks=0; start_btn(NULL); start_btn(NULL); assert(tasks==1 && strcmp(task_url,OTA_URL_BETA)==0);
        assert(lv_obj_has_state(g_switch,LV_STATE_DISABLED));
        s_task_alive=false; create_result=0; start_btn(NULL); ota_tick();
        assert(status_snapshot().error==ESP_ERR_NO_MEM && !s_task_alive);
        assert(!lv_obj_has_state(g_switch,LV_STATE_DISABLED));
        create_result=pdPASS; ota_exit(); lv_obj_delete(page);
    }
    puts("8 actual OTA page renders and task/button guards passed");
    return 0;
}
