// 真实队列/状态模块，虚拟调度只控制worker执行点；不访问设备或实际NVS。
// Real queue/state modules; deterministic scheduling controls worker steps without hardware/NVS access.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "nvs.h"
#include "settings.h"
#include "garden_state.h"
#include "focus_state.h"
#include "../../main/ui_persistence.c"
static TickType_t clock_tick;
static bool create_failed;
static unsigned writes,notifications,attempts;
static int commit_error;
static uint8_t values[8],staged[8],blob[32],staged_blob[32];
static bool have_blob;
static uint32_t prefs=1,staged_prefs;
static const char *const keys[]={"bright","vol","face","idle","silent","beta","lang","dice"};
TickType_t xTaskGetTickCount(void){return clock_tick;}
BaseType_t xTaskCreate(void (*worker)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *out){
    assert(worker && !strcmp(name,"ui_store") && stack==3072 && !arg && priority==1);
    if(create_failed)return 0;
    *out=(void *)1;return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t task){assert(task==(void *)1);++notifications;}
unsigned ulTaskNotifyTake(BaseType_t clear,TickType_t wait){(void)clear;(void)wait;assert(false);return 0;}
static void drain(void){TickType_t wait;unsigned steps=0;while(store_step(&wait))assert(++steps<20);}
void vTaskDelay(TickType_t ticks){clock_tick+=ticks;drain();}
void display_set_brightness(uint8_t brightness){assert(brightness>=SETTINGS_BRIGHT_MIN);}
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *out){
    (void)mode;assert(!s_mux);*out=!strcmp(name,"settings")?1:!strcmp(name,"garden")?2:!strcmp(name,"focus")?3:0;assert(*out);return ESP_OK;
}
static unsigned key_index(const char *name){for(unsigned i=0;i<8;i++)if(!strcmp(keys[i],name))return i;assert(false);return 0;}
esp_err_t nvs_get_u8(nvs_handle_t h,const char *key,uint8_t *out){assert(h==1);*out=values[key_index(key)];return ESP_OK;}
esp_err_t nvs_set_u8(nvs_handle_t h,const char *key,uint8_t value){assert(h==1 && !s_mux);staged[key_index(key)]=value;++writes;return ESP_OK;}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *size){
    assert(h==2 && !strcmp(key,"state_v1"));if(!have_blob)return ESP_ERR_NVS_NOT_FOUND;
    assert(*size>=32);memcpy(out,blob,32);*size=32;return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *value,size_t size){assert(h==2 && !s_mux && !strcmp(key,"state_v1") && size==32);memcpy(staged_blob,value,32);++writes;return ESP_OK;}
esp_err_t nvs_get_u32(nvs_handle_t h,const char *key,uint32_t *out){assert(h==3 && !strcmp(key,"prefs_v1"));*out=prefs;return ESP_OK;}
esp_err_t nvs_set_u32(nvs_handle_t h,const char *key,uint32_t value){assert(h==3 && !s_mux && !strcmp(key,"prefs_v1"));staged_prefs=value;++writes;return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h){
    ++attempts;if(commit_error)return commit_error;
    if(h==1)memcpy(values,staged,8);else if(h==2){memcpy(blob,staged_blob,32);have_blob=true;}else prefs=staged_prefs;
    return ESP_OK;
}
void nvs_close(nvs_handle_t h){assert(h>=1 && h<=3);}
static unsigned observed,probe_calls;
static bool inject,fail_probe;
static bool probe(const void *data){
    unsigned value=*(const unsigned *)data;observed=value;++probe_calls;
    if(inject){inject=false;unsigned next=value+1;assert(!ui_persistence_save(UI_STORE_SETTINGS,&next,sizeof next,probe));assert(*(const unsigned *)data==value);}
    return !fail_probe;
}
int main(void){
    unsigned data=10;create_failed=true;
    assert(!ui_persistence_save(UI_STORE_SETTINGS,&data,sizeof data,probe));assert(!s_worker);
    assert(!ui_persistence_flush(100));create_failed=false;assert(ui_persistence_flush(100) && observed==10);probe_calls=0;data=11;
    assert(!ui_persistence_save(UI_STORE_SETTINGS,&data,sizeof data,probe));data=99;
    clock_tick+=74;drain();assert(!probe_calls);++clock_tick;drain();assert(observed==11 && probe_calls==1);
    data=11;assert(ui_persistence_save(UI_STORE_SETTINGS,&data,sizeof data,probe));
    data=20;assert(!ui_persistence_save(UI_STORE_SETTINGS,&data,sizeof data,probe));
    clock_tick+=20;data=30;assert(!ui_persistence_save(UI_STORE_SETTINGS,&data,sizeof data,probe));
    clock_tick+=75;drain();assert(observed==30 && probe_calls==2);
    inject=true;data=40;assert(!ui_persistence_save(UI_STORE_SETTINGS,&data,sizeof data,probe));clock_tick+=75;drain();
    data=41;assert(!ui_persistence_saved(UI_STORE_SETTINGS,&data,sizeof data));clock_tick+=75;drain();assert(observed==41 && ui_persistence_saved(UI_STORE_SETTINGS,&data,sizeof data));
    fail_probe=true;data=50;assert(!ui_persistence_save(UI_STORE_SETTINGS,&data,sizeof data,probe));unsigned before=probe_calls;
    assert(!ui_persistence_flush(1000) && probe_calls==before+3);unsigned pending=0;
    assert(ui_persistence_pending(UI_STORE_SETTINGS,&pending,sizeof pending) && pending==50);
    fail_probe=false;assert(ui_persistence_flush(1000));
    clock_tick=UINT32_MAX-30;data=60;assert(!ui_persistence_save(UI_STORE_SETTINGS,&data,sizeof data,probe));clock_tick+=100;drain();assert(observed==60);
    data=70;assert(!ui_persistence_save(UI_STORE_SETTINGS,&data,sizeof data,probe));assert(!ui_persistence_flush(0));assert(ui_persistence_flush(1000));
    assert(!ui_persistence_save(UI_STORE_COUNT,&data,sizeof data,probe));assert(!ui_persistence_save(UI_STORE_SETTINGS,&data,33,probe));

    // 从实际设置/成长/专注入口验证：UI不写NVS，最终副本、失败与重新进页保持正确。
    // Exercise real callers: the UI never writes NVS; final copies, failures and re-entry remain correct.
    values[0]=0xc0;values[1]=60;settings_init();before=writes;settings_set_face(7);settings_save();settings_set_face(8);settings_save();assert(writes==before);
    assert(ui_persistence_flush(1000) && values[2]==8);before=writes;settings_set_face(9);settings_save();assert(ui_persistence_flush(1000) && writes==before+1 && values[2]==9);
    garden_state_t *garden=garden_shared_state();assert(garden_shared_ready() && garden_state_water(garden));garden_shared_mark_dirty();before=writes;
    assert(!garden_shared_save() && garden_shared_dirty() && writes==before);assert(ui_persistence_flush(1000) && !garden_shared_dirty());
    garden_state_t loaded;assert(garden_state_load(&loaded) && loaded.waterings==1);
    focus_preferences_t preferences={3,true},loaded_preferences={0};before=writes;
    assert(!focus_preferences_save_async(&preferences) && writes==before);assert(focus_preferences_load(&loaded_preferences) && loaded_preferences.scene==3 && loaded_preferences.muted);
    commit_error=ESP_FAIL;assert(!ui_persistence_flush(1000));assert(focus_preferences_load(&loaded_preferences) && loaded_preferences.scene==3);
    commit_error=0;assert(ui_persistence_flush(1000) && focus_preferences_save_async(&preferences));
    assert(focus_preferences_load(&loaded_preferences) && loaded_preferences.scene==3 && loaded_preferences.muted);
    assert(notifications && attempts);puts("Deferred store: immutable/coalesced snapshots, in-flight edits, create failure, bounded retry, tick wrap, exit barriers and real settings/garden/focus callers passed.");
}
