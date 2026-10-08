#include "startup.h"
#include "startup_test_sdk.h"
#include "app.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static int64_t now,image_until;
static int64_t background_until;
static uint8_t face;
static bool background_started;
static bool audio=true,wifi=true,psram=true,integrity=true,frames=true,frozen,partition_present=true,rollback_possible=true;
static uint32_t allocation_failure;
static int allocations,storage_handles,confirmations,rollbacks;
static esp_err_t storage_error,state_error,confirmation_error,rollback_error;
static esp_ota_img_states_t image_state=ESP_OTA_IMG_VALID;
static esp_partition_t partition;
static void enter(lv_obj_t *parent){(void)parent;}
static void leave(void){}
static app_t test_app={.name="test",.enter=enter,.exit=leave};
const app_t *const APPS[]={&test_app};
const int APP_COUNT=1;
bool audio_bus_ready(void){return audio;}
bool wifi_service_initialized(void){return wifi;}
bool esp_psram_is_initialized(void){return psram;}
bool heap_caps_check_integrity(uint32_t caps,bool print_errors){assert(caps==MALLOC_CAP_INTERNAL && print_errors);return integrity;}
void *heap_caps_malloc(size_t size,uint32_t caps){assert(size==512);if(caps&allocation_failure)return NULL;++allocations;return malloc(size);}
void heap_caps_free(void *memory){assert(memory && allocations>0);--allocations;free(memory);}
bool img_store_loading(void){return now<image_until;}
uint8_t settings_face(void){return face;}
int watchface_count(void){return 15;}
const lv_image_dsc_t *img_store_face_image_for(int theme){assert(theme>=0 && theme<3);if(now>=image_until)background_started=true;return NULL;}
bool img_store_face_loading(int theme){assert(theme>=0 && theme<3);return now<image_until || (background_started && now<background_until);}
uint32_t launcher_heartbeat(void){return frozen && now>500000?25:(uint32_t)(now/20000);}
uint32_t display_transfer_count(void){return frames && now>=25000?1:0;}
static bool home_complete=true;
static int64_t home_after;
bool display_frame_completed(uint32_t ticket){return ticket && home_complete && now>=home_after;}
int64_t esp_timer_get_time(void){return now;}
void vTaskDelay(TickType_t ticks){assert(ticks==25);now+=(int64_t)ticks*1000;}
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *handle){assert(!strcmp(name,"settings") && mode==NVS_READONLY);if(storage_error)return storage_error;*handle=7;++storage_handles;return ESP_OK;}
void nvs_close(nvs_handle_t handle){assert(handle==7 && storage_handles==1);--storage_handles;}
const esp_partition_t *esp_ota_get_running_partition(void){return partition_present?&partition:NULL;}
esp_err_t esp_ota_get_state_partition(const esp_partition_t *p,esp_ota_img_states_t *state){assert(p==&partition);if(state_error)return state_error;*state=image_state;return ESP_OK;}
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void){++confirmations;return confirmation_error;}
bool esp_ota_check_rollback_is_possible(void){return rollback_possible;}
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void){++rollbacks;return rollback_error;}
static startup_result_t check(void){now=0;startup_result_t result=startup_selftest(true,true,0,0,0);assert(!allocations && !storage_handles);return result;}
static void expect(startup_problem_t expected){assert(check().problem==expected);}
int main(void) {
    now=0;assert(startup_wait_home(0,1).problem==STARTUP_OK && now==25000);
    home_complete=false;now=0;assert(startup_wait_home(0,1).problem==STARTUP_UI && now==5000000);
    home_complete=true;now=0;assert(startup_wait_home(0,0).problem==STARTUP_UI && now==5000000);
    home_after=3000000;frozen=true;now=0;
    assert(startup_wait_home(0,1).problem==STARTUP_UI && now==5000000);
    home_after=0;frozen=false;
    startup_result_t result=check();assert(result.problem==STARTUP_OK && now==4000000);
    image_until=4400000;expect(STARTUP_OK);assert(now==image_until);
    image_until=6000000;expect(STARTUP_IMAGE);assert(now==5000000);image_until=0;
    for(face=4;face<15;face+=5) {
        background_started=false;image_until=3900000;background_until=4400000;
        expect(STARTUP_OK);assert(background_started && now==4400000);
    }
    face=255;background_started=false;image_until=0;background_until=6000000;
    expect(STARTUP_IMAGE);assert(now==5000000);face=0;background_until=0;
    frozen=true;expect(STARTUP_UI);assert(now==5000000);frozen=false;
    frames=false;expect(STARTUP_UI);frames=true;
    assert(startup_selftest(false,true,0,0,0).problem==STARTUP_TOUCH);
    assert(startup_selftest(true,false,0,0,0).problem==STARTUP_UI);
    test_app.enter=NULL;expect(STARTUP_APPS);test_app.enter=enter;
    test_app.exit=NULL;expect(STARTUP_OK);test_app.exit=leave; // Existing stateless calendar contract.
    test_app.name=NULL;expect(STARTUP_APPS);test_app.name="test";
    storage_error=ESP_FAIL;expect(STARTUP_STORAGE);
    storage_error=ESP_ERR_NVS_NOT_FOUND;expect(STARTUP_OK);storage_error=ESP_OK;
    audio=false;expect(STARTUP_AUDIO);audio=true;
    wifi=false;expect(STARTUP_WIFI);wifi=true;
    psram=false;expect(STARTUP_MEMORY);psram=true;
    integrity=false;expect(STARTUP_MEMORY);integrity=true;
    allocation_failure=MALLOC_CAP_INTERNAL;expect(STARTUP_MEMORY);
    allocation_failure=MALLOC_CAP_SPIRAM;expect(STARTUP_MEMORY);allocation_failure=0;
    result=check();assert(result.problem==STARTUP_OK); // IP/Internet are not core gates.
    startup_ota_result_t ota=startup_apply_ota_result(result);assert(ota.action==STARTUP_OTA_NONE && !confirmations && !rollbacks);
    image_state=ESP_OTA_IMG_PENDING_VERIFY;ota=startup_apply_ota_result(result);
    assert(ota.action==STARTUP_OTA_CONFIRMED && confirmations==1 && !rollbacks);
    confirmation_error=ESP_FAIL;ota=startup_apply_ota_result(result);
    assert(ota.action==STARTUP_OTA_CONFIRM_ERROR && ota.error==ESP_FAIL && !rollbacks);confirmation_error=ESP_OK;
    startup_result_t failed={STARTUP_UI,ESP_ERR_TIMEOUT};ota=startup_apply_ota_result(failed);
    assert(ota.action==STARTUP_OTA_ROLLBACK && rollbacks==1);
    rollback_possible=false;ota=startup_apply_ota_result(failed);
    assert(ota.action==STARTUP_OTA_ROLLBACK_ERROR && rollbacks==1);rollback_possible=true;
    rollback_error=ESP_FAIL;ota=startup_apply_ota_result(failed);
    assert(ota.action==STARTUP_OTA_ROLLBACK_ERROR && rollbacks==2);rollback_error=ESP_OK;
    image_state=ESP_OTA_IMG_VALID;int previous=confirmations;ota=startup_apply_ota_result(failed);
    assert(ota.action==STARTUP_OTA_NONE && confirmations==previous && rollbacks==2);
    state_error=ESP_ERR_NOT_FOUND;ota=startup_apply_ota_result(result);assert(ota.action==STARTUP_OTA_NONE);
    state_error=ESP_FAIL;ota=startup_apply_ota_result(result);assert(ota.action==STARTUP_OTA_STATE_ERROR);
    state_error=ESP_OK;partition_present=false;ota=startup_apply_ota_result(result);assert(ota.action==STARTUP_OTA_STATE_ERROR);
    assert(!strcmp(startup_problem_name(STARTUP_MEMORY),"MEMORY"));
    assert(!strcmp(startup_problem_name((startup_problem_t)99),"UNKNOWN"));
    puts("Startup: minimum/slow init, fresh heartbeat/DMA, memory/storage/resources, offline core acceptance, OTA confirmation/rollback/error isolation and cleanup passed");
}
