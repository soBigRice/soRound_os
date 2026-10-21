// 原UI/低层NVS边界用例使用即时调度；真实异步队列另由persistence_tests覆盖。
// Existing UI/NVS boundary cases use immediate scheduling; persistence_tests exercises the real asynchronous queue.
#include "ui_persistence.h"
#include <stdint.h>
#include <string.h>
static uint8_t copies[UI_STORE_COUNT][32];
static size_t sizes[UI_STORE_COUNT];
static bool dirty[UI_STORE_COUNT];
static bool deferred;
static ui_store_writer_t writers[UI_STORE_COUNT];
void test_persistence_defer(bool enabled){deferred=enabled;}
bool ui_persistence_save(ui_store_key_t key,const void *data,size_t size,ui_store_writer_t writer){
    if(key>=UI_STORE_COUNT || size>32)return false;
    memcpy(copies[key],data,size);sizes[key]=size;writers[key]=writer;dirty[key]=deferred || !writer(data);return !dirty[key];
}
bool ui_persistence_saved(ui_store_key_t key,const void *data,size_t size){return !dirty[key] && sizes[key]==size && !memcmp(data,copies[key],size);}
bool ui_persistence_pending(ui_store_key_t key,void *data,size_t size){if(!dirty[key] || sizes[key]!=size)return false;memcpy(data,copies[key],size);return true;}
bool ui_persistence_flush(unsigned timeout_ms){(void)timeout_ms;bool saved=true;for(unsigned i=0;i<UI_STORE_COUNT;i++)if(dirty[i]){dirty[i]=!writers[i](copies[i]);saved&=!dirty[i];}return saved;}
