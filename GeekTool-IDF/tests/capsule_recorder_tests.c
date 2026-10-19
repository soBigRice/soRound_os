#include "audio_sdk.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "../main/capsule_recorder.c"
static jmp_buf finished;
static unsigned scenario,reads,acquires,releases,mic_stops,write_samples,starts,unmounts;
static bool dispatched,saved,finished_record;
static capsule_snapshot_t final_snapshot;
static audio_wav_state_t wav_state;
uint8_t settings_silent(void){return 0;}
void *board_i2c_bus(void){return (void *)1;}
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle) {
    assert(fn==recorder_worker&&!strcmp(name,"capsules")&&stack==6144&&!arg&&priority==4);
    *(TaskHandle_t *)handle=(void *)1;return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t handle){assert(handle);}
void vTaskDelete(void *task){assert(!task);longjmp(finished,1);}
void vTaskDelay(unsigned ms) {
    if(ms==20){if(scenario==5)capsule_recorder_stop();else wav_state=AUDIO_WAV_DONE;}
    else assert(ms==10);
}
unsigned ulTaskNotifyTake(int clear,unsigned timeout) {
    assert(clear==pdTRUE&&timeout==50);
    if(!dispatched) {
        dispatched=true;
        if(scenario==4 || scenario==5)assert(capsule_recorder_play("1234ABCD.WAV"));
        else assert(capsule_recorder_record());
    } else {capsule_recorder_snapshot(&final_snapshot);capsule_recorder_exit();}
    return 1;
}
bool audio_bus_acquire(TickType_t timeout){assert(timeout==50);++acquires;return scenario!=2;}
void audio_bus_release(void){++releases;}
bool audio_mic_start(i2c_master_bus_handle_t bus){assert(bus);++starts;return true;}
void audio_mic_stop(void){++mic_stops;}
int audio_mic_read(int16_t *pcm,int count) {
    ++reads;if(scenario==3)return 0;
    for(int i=0;i<count;++i)pcm[i]=(int16_t)(i*30-4000);
    if(reads==4){if(scenario==1)capsule_recorder_exit();else assert(capsule_recorder_stop());}
    return count;
}
capsule_result_t capsule_store_mount(void){return CAPSULE_OK;}
void capsule_store_unmount(void){++unmounts;}
capsule_result_t capsule_store_list(capsule_catalog_t *catalog){memset(catalog,0,sizeof *catalog);catalog->free_bytes=7000000;return CAPSULE_OK;}
capsule_result_t capsule_store_begin(void){return CAPSULE_OK;}
capsule_result_t capsule_store_write(const int16_t *pcm,size_t n){assert(pcm&&n==256);if(scenario==6 && write_samples>=512)return CAPSULE_FULL;write_samples+=(unsigned)n;return CAPSULE_OK;}
capsule_result_t capsule_store_finish(bool save){saved=save;finished_record=true;return CAPSULE_OK;}
capsule_result_t capsule_store_delete(const char *name){assert(capsule_name_valid(name));return CAPSULE_OK;}
bool capsule_store_path(const char *name,char path[CAPSULE_PATH_BYTES]){assert(capsule_name_valid(name));snprintf(path,CAPSULE_PATH_BYTES,"/caps/%s",name);return true;}
bool audio_out_wav_start(const char *path){assert(capsule_path_valid(path));wav_state=AUDIO_WAV_QUEUED;return true;}
void audio_out_wav_stop(void){wav_state=AUDIO_WAV_CANCELLED;}
audio_wav_state_t audio_out_wav_state(void){return wav_state;}
static void run(unsigned test) {
    scenario=test;reads=acquires=releases=mic_stops=write_samples=starts=unmounts=0;
    dispatched=saved=finished_record=false;memset(&final_snapshot,0,sizeof final_snapshot);
    capsule_recorder_enter();assert(!capsule_recorder_play("../bad.wav"));
    if(!setjmp(finished))recorder_worker(NULL);
    assert(!s_worker&&unmounts==1);
}
int main(void) {
    run(0);assert(saved&&finished_record&&write_samples==1024&&reads==4&&starts==1&&releases==1&&mic_stops==1);
    assert(final_snapshot.saved&&final_snapshot.state==CAPSULE_READY&&final_snapshot.waveform[31]>0);
    run(1);assert(finished_record&&!saved&&releases==1&&mic_stops==1);
    run(2);assert(acquires==30&&!reads&&!starts&&!releases&&final_snapshot.mic_failed);
    run(3);assert(reads==3&&finished_record&&!saved&&final_snapshot.mic_failed&&releases==1);
    run(4);assert(!starts&&final_snapshot.state==CAPSULE_READY);
    run(5);assert(!starts&&final_snapshot.state==CAPSULE_READY);
    run(6);assert(saved&&write_samples==512&&final_snapshot.saved&&final_snapshot.result==CAPSULE_FULL);
    puts("capsule recorder: actual worker capture/peak waveform, save versus exit cancellation, bounded audio handoff/read failures, playback/stop and task/codec/bus cleanup passed");
}
