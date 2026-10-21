#include "ui_persistence.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>
#include <string.h>

#define SNAPSHOT_BYTES 32
#define SETTLE_MS 75
#define MAX_WAIT_MS 500
#define RETRY_MS 250
#define MAX_ATTEMPTS 3
typedef struct {
    uint32_t data[SNAPSHOT_BYTES/4],generation;
    size_t size;ui_store_writer_t writer;
    TickType_t due,first;
    unsigned attempts;
    bool dirty,failed;
} store_slot_t;
static store_slot_t s_slots[UI_STORE_COUNT];
static portMUX_TYPE s_mux=portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_worker;
static bool s_starting;
static unsigned s_cursor;

static bool valid_key(ui_store_key_t key){return key>=0 && key<UI_STORE_COUNT;}
static bool same(const store_slot_t *slot,const void *data,size_t size){return slot->size==size && !memcmp(slot->data,data,size);}

static bool store_step(TickType_t *wait) {
    uint32_t snapshot[SNAPSHOT_BYTES/4],generation=0;ui_store_writer_t writer=NULL;
    TickType_t now=xTaskGetTickCount();unsigned selected=UI_STORE_COUNT;
    *wait=portMAX_DELAY;
    portENTER_CRITICAL(&s_mux);
    for(unsigned n=0;n<UI_STORE_COUNT;n++){
        unsigned i=(s_cursor+n)%UI_STORE_COUNT;store_slot_t *slot=&s_slots[i];
        if(!slot->dirty || slot->failed)continue;
        int32_t remaining=(int32_t)(slot->due-now);
        if(remaining>0){if((TickType_t)remaining<*wait)*wait=(TickType_t)remaining;continue;}
        selected=i;generation=slot->generation;writer=slot->writer;
        memcpy(snapshot,slot->data,slot->size);s_cursor=(i+1)%UI_STORE_COUNT;break;
    }
    portEXIT_CRITICAL(&s_mux);
    if(selected==UI_STORE_COUNT)return false;
    // NVS始终在临界区外；UI后续修改不会改变正在写的快照。
    // NVS runs outside the critical section; later UI edits cannot mutate an in-flight snapshot.
    bool saved=writer(snapshot);
    portENTER_CRITICAL(&s_mux);
    store_slot_t *slot=&s_slots[selected];
    if(slot->generation==generation){
        if(saved){slot->dirty=false;slot->failed=false;}
        else if(++slot->attempts>=MAX_ATTEMPTS)slot->failed=true;
        else slot->due=xTaskGetTickCount()+pdMS_TO_TICKS(RETRY_MS);
    }
    portEXIT_CRITICAL(&s_mux);return true;
}
static void store_worker(void *arg) {
    (void)arg;
    for(;;){TickType_t wait;if(!store_step(&wait))ulTaskNotifyTake(pdTRUE,wait);}
}
static bool ensure_worker(void) {
    portENTER_CRITICAL(&s_mux);
    bool ready=s_worker!=NULL,create=!ready&&!s_starting;
    if(create)s_starting=true;
    portEXIT_CRITICAL(&s_mux);
    if(create){
        TaskHandle_t worker=NULL;
        BaseType_t result=xTaskCreate(store_worker,"ui_store",3072,NULL,1,&worker);
        portENTER_CRITICAL(&s_mux);s_worker=result==pdPASS?worker:NULL;s_starting=false;ready=s_worker!=NULL;portEXIT_CRITICAL(&s_mux);
    }
    return ready;
}
bool ui_persistence_save(ui_store_key_t key,const void *snapshot,size_t size,ui_store_writer_t writer) {
    if(!valid_key(key)||!snapshot||!writer||!size||size>SNAPSHOT_BYTES)return false;
    TickType_t now=xTaskGetTickCount();bool saved;
    portENTER_CRITICAL(&s_mux);store_slot_t *slot=&s_slots[key];
    if(!same(slot,snapshot,size) || slot->writer!=writer){
        if(!slot->dirty || slot->failed)slot->first=now;
        memcpy(slot->data,snapshot,size);slot->size=size;slot->writer=writer;++slot->generation;
        slot->dirty=true;slot->failed=false;slot->attempts=0;
        TickType_t age=now-slot->first,delay=pdMS_TO_TICKS(SETTLE_MS),limit=pdMS_TO_TICKS(MAX_WAIT_MS);
        if(age>=limit)delay=0;else if(delay>limit-age)delay=limit-age;
        slot->due=now+delay;
    }else if(slot->failed){slot->failed=false;slot->attempts=0;slot->first=now;slot->due=now+pdMS_TO_TICKS(SETTLE_MS);}
    saved=!slot->dirty;portEXIT_CRITICAL(&s_mux);
    if(!saved && ensure_worker())xTaskNotifyGive(s_worker);
    return saved;
}
bool ui_persistence_saved(ui_store_key_t key,const void *snapshot,size_t size) {
    if(!valid_key(key)||!snapshot||!size||size>SNAPSHOT_BYTES)return false;
    portENTER_CRITICAL(&s_mux);bool saved=!s_slots[key].dirty && same(&s_slots[key],snapshot,size);portEXIT_CRITICAL(&s_mux);return saved;
}
bool ui_persistence_pending(ui_store_key_t key,void *snapshot,size_t size) {
    if(!valid_key(key)||!snapshot||!size||size>SNAPSHOT_BYTES)return false;
    portENTER_CRITICAL(&s_mux);store_slot_t *slot=&s_slots[key];bool pending=slot->dirty && slot->size==size;
    if(pending)memcpy(snapshot,slot->data,size);
    portEXIT_CRITICAL(&s_mux);return pending;
}
bool ui_persistence_flush(unsigned timeout_ms) {
    TickType_t started=xTaskGetTickCount();bool dirty=false;
    portENTER_CRITICAL(&s_mux);
    for(unsigned i=0;i<UI_STORE_COUNT;i++)if(s_slots[i].dirty){dirty=true;s_slots[i].due=started;s_slots[i].failed=false;s_slots[i].attempts=0;}
    portEXIT_CRITICAL(&s_mux);
    if(!dirty)return true;
    if(!ensure_worker())return false;
    xTaskNotifyGive(s_worker);
    for(;;){
        bool failed=false;dirty=false;
        portENTER_CRITICAL(&s_mux);
        for(unsigned i=0;i<UI_STORE_COUNT;i++){dirty|=s_slots[i].dirty;failed|=s_slots[i].failed;}
        portEXIT_CRITICAL(&s_mux);
        if(!dirty)return true;
        if(failed || xTaskGetTickCount()-started>=pdMS_TO_TICKS(timeout_ms))return false;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
