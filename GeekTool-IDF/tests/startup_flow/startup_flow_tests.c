// Full production app_main -> launcher -> lock/watchface -> cover -> OTA gate.
// Device/RTOS services are fixtures; widgets, registry, timers and renderers are real.
#include "flow_sdk.h"
#include "app.h"
#include "power.h"
#include "buttons.h"
#include "settings.h"
#include "identity_ui.h"
#include "startup_network.h"
#include "display.h"
#include "watchface.h"
#include "lock.h"
#include "img_store.h"
#include "tools_ui.h"
#include "esp_heap_caps.h"
#include "font_companion.h"
#include "src/misc/lv_text_private.h"
#include <stdio.h>
LV_FONT_DECLARE(font_location_24);
static int64_t now;
static bool held,wifi,home_dma,ui_lock_failure;
static unsigned lock_calls;
static unsigned confirmations,rollbacks,transfers;
static unsigned memory_samples,memory_caps;
static uint8_t face,language;
static uint32_t requested,completed;
static lv_display_t *display;
static uint16_t pixels[466*466];
_Alignas(LV_DRAW_BUF_ALIGN) static uint16_t buffers[2][466*40];
static void fixture_enter(lv_obj_t *parent){(void)parent;}
#define APP(n) const app_t app_##n={.name=#n,.enter=fixture_enter}
APP(wifi);APP(i2c);APP(sys);APP(weather);APP(calendar);APP(countdown);APP(stopwatch);APP(settings);APP(ota);APP(audio);APP(level);APP(maze);APP(fluid);APP(dice);APP(mouse);APP(twin);APP(answers);APP(zodiac);APP(merit);APP(pixels);
// 名称必须与生产注册一致，否则中文映射不会覆盖新增应用。
// Match production names so the fixture exercises localized new-app labels.
const app_t app_garden={.name="Garden",.enter=fixture_enter};
const app_t app_focus={.name="Focus",.enter=fixture_enter};
const app_t app_capsules={.name="Capsules",.enter=fixture_enter};
#undef APP
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *bytes) {
    unsigned w=(unsigned)lv_area_get_width(a);
    for(int y=a->y1;y<=a->y2;++y) {
        for(unsigned x=0;x<w;++x)pixels[y*466+a->x1+x]=(uint16_t)((bytes[x*2]<<8)|bytes[x*2+1]);
        bytes+=w*2;
    }
    ++transfers;
    if(lv_display_flush_is_last(d) && requested && home_dma)completed=requested;
    lv_display_flush_ready(d);
}
lv_display_t *display_init(void) {
    lv_init();display=lv_display_create(466,466);assert(display);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_buffers(display,buffers[0],buffers[1],sizeof buffers[0],LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);return display;
}
bool touch_init(i2c_master_bus_handle_t bus,lv_display_t *d){assert(bus && d==display);return true;}
uint32_t display_transfer_count(void){return transfers;}
uint32_t display_request_frame(void) {
    assert(held && identity_boot_active());++requested;
    lv_obj_invalidate(lv_screen_active());lv_obj_invalidate(lv_layer_top());return requested;
}
bool display_frame_completed(uint32_t ticket){return ticket && ticket==completed;}
void display_set_brightness(uint8_t value){assert(value>=64);}
void display_sleep(bool sleep){assert(!sleep);}
bool lvgl_port_lock(uint32_t timeout) {
    assert(timeout>0 && timeout<=5000 && !held);
    if(ui_lock_failure && ++lock_calls>1) { now+=(int64_t)timeout*1000;return false; }
    held=true;return true;
}
void lvgl_port_unlock(void){assert(held);held=false;}
void vTaskDelay(TickType_t ticks) {
    assert(!held);now+=(int64_t)ticks*1000;lv_tick_inc(ticks);lv_timer_handler();
}
int64_t esp_timer_get_time(void){return now;}
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle){assert(fn && !strcmp(name,"rwdt") && stack==2560);(void)arg;(void)priority;(void)handle;return pdPASS;}
esp_err_t nvs_flash_init(void){return ESP_OK;}
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *h){assert(!strcmp(name,"settings") && mode==NVS_READONLY);*h=1;return ESP_OK;}
void nvs_close(nvs_handle_t h){assert(h==1);}
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *cfg,i2c_master_bus_handle_t *bus){assert(cfg->sda_io_num==15 && cfg->scl_io_num==14);*bus=(void *)1;return ESP_OK;}
esp_err_t esp_pm_configure(const esp_pm_config_t *p){assert(p->max_freq_mhz==240);return ESP_OK;}
esp_reset_reason_t esp_reset_reason(void){return 1;}
void esp_restart(void){assert(!"Unexpected reboot");}
esp_err_t esp_pm_lock_create(int type,int arg,const char *name,esp_pm_lock_handle_t *h){(void)type;(void)arg;(void)name;*h=calloc(1,sizeof **h);return *h?ESP_OK:ESP_ERR_NO_MEM;}
esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t h){h->held=1;return ESP_OK;}
esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t h){h->held=0;return ESP_OK;}
size_t heap_caps_get_free_size(unsigned cap){(void)cap;return 100000;}
void heap_caps_get_info(multi_heap_info_t *info,uint32_t caps){
    assert(caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT) || caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    assert(!(memory_caps&caps&~MALLOC_CAP_8BIT));
    memory_caps|=caps;++memory_samples;
    *info=(multi_heap_info_t){.total_free_bytes=100000,.largest_free_block=32000,.minimum_free_bytes=56000};
}
void *heap_caps_malloc(size_t size,uint32_t caps){(void)caps;return malloc(size);}
void heap_caps_free(void *p){free(p);}
bool heap_caps_check_integrity(uint32_t caps,bool errors){assert(caps==MALLOC_CAP_INTERNAL && errors);return true;}
bool esp_psram_is_initialized(void){return true;}
void audio_bus_init(void){}
bool audio_bus_ready(void){return true;}
bool imu_init(void){return true;}
void rtc_begin(void){}
bool rtc_sync_to_system(void){return false;}
void settings_init(void){}
uint8_t settings_lang(void){return language;}
uint8_t settings_face(void){return face;}
uint8_t settings_beta(void){return 1;}
uint8_t settings_brightness(void){return 191;}
uint8_t settings_idle_mode(void){return IDLE_AOD;}
void wifi_service_start(void){wifi=true;}
bool wifi_service_initialized(void){return wifi;}
bool wifi_service_ready(void){return false;}
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap){(void)ap;return ESP_FAIL;}
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *name){(void)name;return NULL;}
esp_err_t esp_netif_get_ip_info(esp_netif_t *n,esp_netif_ip_info_t *ip){(void)n;(void)ip;return ESP_FAIL;}
startup_network_result_t startup_network_check(bool beta){assert(beta && confirmations==0);return (startup_network_result_t){.state=STARTUP_NET_OFFLINE,.error=ESP_OK};}
static const esp_partition_t partition={0};
const esp_partition_t *esp_ota_get_running_partition(void){return &partition;}
esp_err_t esp_ota_get_state_partition(const esp_partition_t *p,esp_ota_img_states_t *state){assert(p==&partition);*state=ESP_OTA_IMG_PENDING_VERIFY;return ESP_OK;}
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void) {
    if(!completed || completed!=requested){fputs("OTA confirmed before the revealed home frame completed\n",stderr);exit(42);}
    assert(identity_boot_active() && watchface_visible() && now>=4000000);
    unsigned black=0,bright=0;for(unsigned i=0;i<466*466;++i) {
        uint16_t p=pixels[i];black+=p==0;
        // The confirmed watchface uses warm white (0xf3f1eb), not pure 0xffffff.
        bright+=((p>>11)&31)>24 && ((p>>5)&63)>50 && (p&31)>24;
    }
    assert(black>100000 && bright>300 && bright<50000);
    ++confirmations;return ESP_OK;
}
bool esp_ota_check_rollback_is_possible(void){return true;}
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void){assert(!confirmations);++rollbacks;return ESP_OK;}
void power_init(void){}
bool power_read(int *soc,pwr_state_t *state){*soc=74;*state=PWR_DISCHARGING;return true;}
void power_charge_govern(void){}
void power_off(void){assert(!"Unexpected power off");}
void buttons_init(void){}
void buttons_reset_control(void){}
button_event_t buttons_poll(bool visible){assert(!visible);return BUTTON_NONE;}
void quickpanel_init(lv_obj_t *top){assert(top==lv_layer_top());}
void quickpanel_hide(void){}
void quickpanel_open(void){}
bool quickpanel_is_open(void){return false;}
void weather_poll(void){}
bool weather_cached(int *a,int *b,int *c,int *d,int *e){*a=*b=*c=*d=*e=0;return false;}
const lv_image_dsc_t *img_store_face_image(void){return NULL;}
bool img_store_loading(void){return false;}
const lv_image_dsc_t *img_store_face_image_for(int theme){assert(theme>=0 && theme<3);return NULL;}
bool img_store_face_loading(int theme){assert(theme>=0 && theme<3);return false;}
void app_main(void);
static lv_indev_state_t pointer_state;
static lv_point_t pointer_point;
static void read_pointer(lv_indev_t *indev,lv_indev_data_t *data) {
    (void)indev;data->point=pointer_point;data->state=pointer_state;
}
static lv_obj_t *find_label(lv_obj_t *root,const char *text) {
    if(lv_obj_check_type(root,&lv_label_class) && !strcmp(lv_label_get_text(root),text))
        return root;
    for(uint32_t i=0;i<lv_obj_get_child_count(root);++i) {
        lv_obj_t *found=find_label(lv_obj_get_child(root,i),text);
        if(found)return found;
    }
    return NULL;
}
static void require_glyphs(lv_obj_t *label) {
    assert(label);const char *text=lv_label_get_text(label);
    const lv_font_t *font=lv_obj_get_style_text_font(label,0);uint32_t at=0;
    while(text[at]) {
        uint32_t cp=lv_text_encoded_next(text,&at);lv_font_glyph_dsc_t dsc;
        if(!lv_font_get_glyph_dsc(font,&dsc,cp,0)||dsc.is_placeholder) {
            fprintf(stderr,"launcher missing U+%04x: %s\n",(unsigned)cp,text);assert(false);
        }
    }
}
static void capture_launcher_name(const char *dir,const char *app) {
    if(!dir)return;
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(display);
    char path[1024];snprintf(path,sizeof path,"%s/launcher-%s-%s.ppm",dir,app,language?"zh":"en");
    FILE *file=fopen(path,"wb");assert(file);fprintf(file,"P6\n466 466\n255\n");
    for(unsigned i=0;i<466*466;++i) {
        uint16_t p=pixels[i];uint8_t rgb[]={((p>>11)&31)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};
        fwrite(rgb,1,sizeof rgb,file);
    }
    assert(fclose(file)==0);
}
static void check_launcher_names(const char *dir) {
    lock_set(false);lv_obj_t *home=lv_screen_active();lv_obj_update_layout(home);
    lv_obj_t *name=lv_obj_get_child(home,1),*next=lv_obj_get_child(home,3),*icon=lv_obj_get_child(home,0);
    assert(lv_obj_check_type(name,&lv_label_class));
    lv_indev_t *pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(pointer,display);lv_indev_set_read_cb(pointer,read_pointer);
    for(unsigned pass=0;pass<2;++pass) {
        language=(uint8_t)(1-pass);
        for(int index=0;index<APP_COUNT;++index) {
            assert(!strcmp(lv_label_get_text(name),tr_app_name(APPS[index]->name)));require_glyphs(name);
            assert(lv_obj_get_style_text_font(name,0)==(index>=20?&font_companion_24:&font_location_24));
            if(index>=20) {
                capture_launcher_name(dir,APPS[index]->name);
                lv_obj_update_layout(home);lv_area_t area;lv_obj_get_coords(icon,&area);
                pointer_point=(lv_point_t){(area.x1+area.x2)/2,(area.y1+area.y2)/2};
                pointer_state=LV_INDEV_STATE_PRESSED;lv_indev_read(pointer);
                pointer_state=LV_INDEV_STATE_RELEASED;lv_indev_read(pointer);
                assert(lv_screen_active()!=home);
                lv_obj_t *title=find_label(lv_layer_top(),tr_app_name(APPS[index]->name));require_glyphs(title);
                assert(lv_obj_get_style_text_font(title,0)==&font_companion_24);
                go_home();assert(lv_screen_active()==home);require_glyphs(name);
            }
            // 用真实按钮/动画推进到下一项，覆盖中点换字和末尾回绕。
            // Drive the real button and animation, covering midpoint updates and wrap-around.
            lv_obj_send_event(next,LV_EVENT_CLICKED,NULL);
            for(unsigned step=0;step<15;++step)vTaskDelay(20);
        }
    }
    lv_indev_delete(pointer);puts("Real launcher bilingual names, animated swaps, new-app titles and return glyphs passed");
}
int main(int argc,char **argv) {
    face=argc>1?(uint8_t)atoi(argv[1]):0;language=argc>2?(uint8_t)atoi(argv[2]):0;
    home_dma=argc<4 || strcmp(argv[3],"stall");
    ui_lock_failure=argc>3 && !strcmp(argv[3],"lock-stall");
    app_main();assert(!held && APP_COUNT==23 && APPS[19]==&app_pixels && APPS[20]==&app_garden &&
        APPS[21]==&app_focus && APPS[22]==&app_capsules && lock_is_locked());
    if(home_dma && !ui_lock_failure){assert(confirmations==1 && !rollbacks && !identity_boot_active() && watchface_visible());assert(memory_samples==2);}
    else {assert(!confirmations && rollbacks==1 && identity_boot_active());assert(!memory_samples);}
    if(argc>3 && !strcmp(argv[3],"launcher"))
        check_launcher_names(argc>4?argv[4]:NULL);
    lv_deinit();puts("Full startup flow and revealed watchface frame/OTA gate passed");return 0;
}
