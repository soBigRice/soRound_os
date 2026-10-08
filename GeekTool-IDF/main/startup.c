#include "startup.h"
#include "identity_ui.h"
#include "app.h"
#include "display.h"
#include "audio_bus.h"
#include "img_store.h"
#include "settings.h"
#include "watchface.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static startup_result_t failed(startup_problem_t problem,esp_err_t error) {
    return (startup_result_t){problem,error};
}
static bool memory_probe(uint32_t caps) {
    // Exercise allocations and a small volatile pattern, not a destructive scan
    // of live RAM or an expensive full-PSRAM heap walk during boot.
    volatile uint8_t *memory=heap_caps_malloc(512,caps|MALLOC_CAP_8BIT);
    if(!memory)return false;
    bool valid=true;
    for(unsigned i=0;i<512;++i)memory[i]=(uint8_t)(i*37u+0x5au);
    for(unsigned i=0;i<512;++i)if(memory[i]!=(uint8_t)(i*37u+0x5au)){valid=false;break;}
    heap_caps_free((void *)memory);return valid;
}
startup_result_t startup_selftest(bool touch_ready,bool launcher_ready,uint32_t first_heartbeat,uint32_t first_transfer,int64_t cover_started_us) {
    if(!touch_ready)return failed(STARTUP_TOUCH,ESP_ERR_NO_MEM);
    if(!launcher_ready)return failed(STARTUP_UI,ESP_ERR_NO_MEM);
    if(APP_COUNT<=0)return failed(STARTUP_APPS,ESP_ERR_INVALID_STATE);
    for(int i=0;i<APP_COUNT;++i)
        // Stateless pages such as calendar have no exit callback by contract.
        if(!APPS[i] || !APPS[i]->name || !APPS[i]->enter)
            return failed(STARTUP_APPS,ESP_ERR_INVALID_STATE);
    nvs_handle_t settings;
    esp_err_t err=nvs_open("settings",NVS_READONLY,&settings);
    if(err==ESP_OK)nvs_close(settings);
    else if(err!=ESP_ERR_NVS_NOT_FOUND)return failed(STARTUP_STORAGE,err);
    if(!audio_bus_ready())return failed(STARTUP_AUDIO,ESP_ERR_INVALID_STATE);
    if(!wifi_service_initialized())return failed(STARTUP_WIFI,ESP_ERR_INVALID_STATE);
    if(!esp_psram_is_initialized() || !heap_caps_check_integrity(MALLOC_CAP_INTERNAL,true) ||
       !memory_probe(MALLOC_CAP_INTERNAL) || !memory_probe(MALLOC_CAP_SPIRAM))
        return failed(STARTUP_MEMORY,ESP_ERR_NO_MEM);
    int64_t deadline=esp_timer_get_time()+5000000;
    int64_t heartbeat_at=esp_timer_get_time();uint32_t heartbeat=first_heartbeat;
    int face=LV_CLAMP(0,settings_face(),watchface_count()-1);
    for(;;) {
        int64_t now=esp_timer_get_time();uint32_t next=launcher_heartbeat();
        if(next!=heartbeat){heartbeat=next;heartbeat_at=now;}
        bool ui=heartbeat!=first_heartbeat && now-heartbeat_at<250000 && display_transfer_count()!=first_transfer;
        bool image=img_store_loading();
        if(face%5==4) {
            // A missing custom image can start the selected built-in background
            // only after its decoder finishes. Include that second worker.
            img_store_face_image_for(face/5);
            image|=img_store_face_loading(face/5);
        }
        if(ui && !image && now-cover_started_us>=IDENTITY_BOOT_MS*1000LL)return (startup_result_t){STARTUP_OK,ESP_OK};
        if(now>=deadline)return failed(ui?STARTUP_IMAGE:STARTUP_UI,ESP_ERR_TIMEOUT);
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}
const char *startup_problem_name(startup_problem_t problem) {
    static const char *const names[]={"OK","TOUCH","APPS","STORAGE","AUDIO","WIFI","MEMORY","UI","IMAGE"};
    return (unsigned)problem<sizeof names/sizeof names[0]?names[problem]:"UNKNOWN";
}
startup_result_t startup_wait_home(uint32_t first_heartbeat,uint32_t frame_request) {
    int64_t deadline=esp_timer_get_time()+5000000;
    int64_t heartbeat_at=esp_timer_get_time();uint32_t heartbeat=first_heartbeat;
    for(;;) {
        int64_t now=esp_timer_get_time();uint32_t next=launcher_heartbeat();
        if(next!=heartbeat){heartbeat=next;heartbeat_at=now;}
        if(display_frame_completed(frame_request) && heartbeat!=first_heartbeat && now-heartbeat_at<250000)
            return (startup_result_t){STARTUP_OK,ESP_OK};
        if(now>=deadline)return failed(STARTUP_UI,ESP_ERR_TIMEOUT);
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}
startup_ota_result_t startup_apply_ota_result(startup_result_t result) {
    const esp_partition_t *running=esp_ota_get_running_partition();
    if(!running)return (startup_ota_result_t){STARTUP_OTA_STATE_ERROR,ESP_ERR_NOT_FOUND};
    esp_ota_img_states_t state;
    esp_err_t err=esp_ota_get_state_partition(running,&state);
    // Direct flashing can leave no otadata entry for this slot.
    if(err==ESP_ERR_NOT_FOUND)return (startup_ota_result_t){STARTUP_OTA_NONE,ESP_OK};
    if(err!=ESP_OK)return (startup_ota_result_t){STARTUP_OTA_STATE_ERROR,err};
    if(state!=ESP_OTA_IMG_PENDING_VERIFY)return (startup_ota_result_t){STARTUP_OTA_NONE,ESP_OK};
    if(result.problem==STARTUP_OK) {
        err=esp_ota_mark_app_valid_cancel_rollback();
        return (startup_ota_result_t){err==ESP_OK?STARTUP_OTA_CONFIRMED:STARTUP_OTA_CONFIRM_ERROR,err};
    }
    if(!esp_ota_check_rollback_is_possible())return (startup_ota_result_t){STARTUP_OTA_ROLLBACK_ERROR,ESP_ERR_OTA_ROLLBACK_FAILED};
    err=esp_ota_mark_app_invalid_rollback_and_reboot();
    // Successful hardware reboot never returns; this branch also supports SDK
    // fault injection without inventing a second, unconditional reboot.
    return (startup_ota_result_t){err==ESP_OK?STARTUP_OTA_ROLLBACK:STARTUP_OTA_ROLLBACK_ERROR,err};
}
