#include "capsule_recorder.h"
#include "audio_mic.h"
#include "audio_bus.h"
#include "audio_out.h"
#include "board_config.h"
#include "settings.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

typedef enum { CMD_NONE,CMD_LOAD,CMD_RECORD,CMD_STOP,CMD_PLAY,CMD_DELETE } command_t;
static portMUX_TYPE s_lock=portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_worker;
static bool s_active;
static uint32_t s_generation;
static command_t s_command;
static char s_name[13];
static capsule_snapshot_t s_snapshot;

static bool current(uint32_t generation) {
    portENTER_CRITICAL(&s_lock);bool live=s_active && generation==s_generation;portEXIT_CRITICAL(&s_lock);return live;
}
static void publish(uint32_t generation,const capsule_snapshot_t *snapshot) {
    portENTER_CRITICAL(&s_lock);if(s_active && generation==s_generation)s_snapshot=*snapshot;portEXIT_CRITICAL(&s_lock);
}
static void refresh(capsule_snapshot_t *snapshot) {
    capsule_result_t r=capsule_store_list(&snapshot->catalog);
    if(r!=CAPSULE_OK && r!=CAPSULE_FULL)snapshot->result=r;
}
static bool stopping(uint32_t generation) {
    portENTER_CRITICAL(&s_lock);bool stop=!s_active || s_generation!=generation || s_command==CMD_STOP;
    if(s_command==CMD_STOP)s_command=CMD_NONE;
    portEXIT_CRITICAL(&s_lock);return stop;
}
static void capture(uint32_t generation,capsule_snapshot_t *snapshot) {
    snapshot->state=CAPSULE_WAITING;snapshot->samples=0;snapshot->saved=snapshot->cancelled=snapshot->mic_failed=false;
    memset(snapshot->waveform,0,sizeof snapshot->waveform);publish(generation,snapshot);
    bool owns=false;
    // 录音只在总线空闲时开始，最多等待1.5秒。 / Bound the I2S handoff to 1.5 seconds.
    for(unsigned i=0;i<30 && current(generation);++i)if(audio_bus_acquire(pdMS_TO_TICKS(50))){owns=true;break;}
    if(!current(generation)){if(owns)audio_bus_release();return;}
    if(!owns){snapshot->state=CAPSULE_ERROR;snapshot->mic_failed=true;publish(generation,snapshot);return;}
    if(!audio_mic_start(board_i2c_bus())) {
        audio_mic_stop();audio_bus_release();snapshot->state=CAPSULE_ERROR;snapshot->mic_failed=true;publish(generation,snapshot);return;
    }
    snapshot->result=capsule_store_begin();
    bool opened=snapshot->result==CAPSULE_OK;
    bool failed=false,stop=false;unsigned missed=0;
    int16_t pcm[256];
    snapshot->state=opened?CAPSULE_RECORDING:CAPSULE_ERROR;publish(generation,snapshot);
    while(opened && !stop && snapshot->samples<CAPSULE_RATE*CAPSULE_MAX_SECONDS) {
        if(stopping(generation)){stop=true;break;}
        int n=audio_mic_read(pcm,256);
        if(n<=0) {
            if(++missed>=3){failed=true;snapshot->mic_failed=true;break;}
            vTaskDelay(pdMS_TO_TICKS(10));continue;
        }
        missed=0;snapshot->result=capsule_store_write(pcm,(size_t)n);
        if(snapshot->result!=CAPSULE_OK){failed=true;break;}
        snapshot->samples+=(uint32_t)n;
        unsigned peak=0;for(int i=0;i<n;++i){int value=pcm[i];unsigned a=(unsigned)(value<0?-value:value);if(a>peak)peak=a;}
        memmove(snapshot->waveform,snapshot->waveform+1,31);
        snapshot->waveform[31]=(uint8_t)(peak*255u/32768u);publish(generation,snapshot);
    }
    audio_mic_stop();audio_bus_release();
    if(opened) {
        // 达到预留容量边界时保存此前完整块；真实短写仍丢弃临时文件。
        // Preserve complete blocks at the reserved-space boundary; discard actual short writes.
        bool capacity_stop=snapshot->result==CAPSULE_FULL && snapshot->samples>0;
        bool save=current(generation) && (!failed || capacity_stop);
        snapshot->state=CAPSULE_SAVING;publish(generation,snapshot);
        capsule_result_t r=capsule_store_finish(save);
        if(!failed)snapshot->result=r;
        snapshot->saved=save && r==CAPSULE_OK;snapshot->cancelled=!save && !failed;
    }
    snapshot->state=(failed || (!opened && snapshot->result!=CAPSULE_OK))?CAPSULE_ERROR:CAPSULE_READY;
    refresh(snapshot);publish(generation,snapshot);
}
static void playback(uint32_t generation,capsule_snapshot_t *snapshot,const char *name) {
    char path[CAPSULE_PATH_BYTES];snapshot->muted=settings_silent();
    if(!capsule_store_path(name,path) || !audio_out_wav_start(path)) {
        snapshot->state=CAPSULE_ERROR;snapshot->result=CAPSULE_INVALID;publish(generation,snapshot);return;
    }
    snapshot->state=CAPSULE_PLAYING;publish(generation,snapshot);
    for(;;) {
        if(stopping(generation))audio_out_wav_stop();
        audio_wav_state_t state=audio_out_wav_state();
        if(state!=AUDIO_WAV_QUEUED && state!=AUDIO_WAV_PLAYING) {
            snapshot->result=state==AUDIO_WAV_FAILED?CAPSULE_IO_ERROR:CAPSULE_OK;
            snapshot->state=state==AUDIO_WAV_FAILED?CAPSULE_ERROR:CAPSULE_READY;break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    publish(generation,snapshot);
}
static void recorder_worker(void *arg) {
    (void)arg;capsule_snapshot_t snapshot={0};
    for(;;) {
        portENTER_CRITICAL(&s_lock);bool active=s_active;uint32_t generation=s_generation;
        command_t command=s_command;s_command=CMD_NONE;char name[13];memcpy(name,s_name,sizeof name);portEXIT_CRITICAL(&s_lock);
        if(!active) {
            audio_out_wav_stop();capsule_store_unmount();
            portENTER_CRITICAL(&s_lock);bool stop=!s_active;if(stop)s_worker=NULL;portEXIT_CRITICAL(&s_lock);
            if(stop){vTaskDelete(NULL);return;}continue;
        }
        if(command==CMD_LOAD) {
            memset(&snapshot,0,sizeof snapshot);snapshot.result=capsule_store_mount();
            snapshot.state=snapshot.result==CAPSULE_OK?CAPSULE_READY:CAPSULE_ERROR;
            if(snapshot.result==CAPSULE_OK)refresh(&snapshot);
            publish(generation,&snapshot);
        } else if(command==CMD_RECORD)capture(generation,&snapshot);
        else if(command==CMD_PLAY)playback(generation,&snapshot,name);
        else if(command==CMD_DELETE) {
            snapshot.result=capsule_store_delete(name);snapshot.state=snapshot.result==CAPSULE_OK?CAPSULE_READY:CAPSULE_ERROR;
            refresh(&snapshot);publish(generation,&snapshot);
        }
        ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(50));
    }
}
void capsule_recorder_enter(void) {
    portENTER_CRITICAL(&s_lock);s_active=true;s_generation++;s_command=CMD_LOAD;
    memset(&s_snapshot,0,sizeof s_snapshot);s_snapshot.state=CAPSULE_LOADING;portEXIT_CRITICAL(&s_lock);
    if(!s_worker && xTaskCreate(recorder_worker,"capsules",6144,NULL,4,&s_worker)!=pdPASS) {
        portENTER_CRITICAL(&s_lock);s_active=false;s_snapshot.state=CAPSULE_ERROR;s_snapshot.result=CAPSULE_IO_ERROR;portEXIT_CRITICAL(&s_lock);
    }
    if(s_worker)xTaskNotifyGive(s_worker);
}
void capsule_recorder_exit(void) {
    portENTER_CRITICAL(&s_lock);s_active=false;s_generation++;s_command=CMD_NONE;portEXIT_CRITICAL(&s_lock);
    if(s_worker)xTaskNotifyGive(s_worker);
}
static bool request(command_t command,const char *name) {
    if(name && !capsule_name_valid(name))return false;
    portENTER_CRITICAL(&s_lock);
    if(command==CMD_STOP && s_active && (s_command==CMD_RECORD || s_command==CMD_PLAY)) {
        // 尚未开始的请求可直接撤销，不留一个永远等待的UI状态。
        // Cancel queued work before it starts, without leaving the UI waiting forever.
        s_command=CMD_NONE;s_snapshot.state=CAPSULE_READY;s_snapshot.cancelled=true;
        portEXIT_CRITICAL(&s_lock);return true;
    }
    bool idle=s_snapshot.state==CAPSULE_READY || s_snapshot.state==CAPSULE_ERROR;
    bool ok=s_active && s_command==CMD_NONE && (idle || command==CMD_STOP);
    if(ok){
        s_command=command;if(name)memcpy(s_name,name,13);
        if(command==CMD_RECORD)s_snapshot.state=CAPSULE_WAITING;
        else if(command==CMD_PLAY)s_snapshot.state=CAPSULE_PLAYING;
        else if(command==CMD_DELETE)s_snapshot.state=CAPSULE_SAVING;
    }
    portEXIT_CRITICAL(&s_lock);if(ok && s_worker)xTaskNotifyGive(s_worker);return ok;
}
bool capsule_recorder_record(void){return request(CMD_RECORD,NULL);}
bool capsule_recorder_stop(void){return request(CMD_STOP,NULL);}
bool capsule_recorder_play(const char *name){return request(CMD_PLAY,name);}
bool capsule_recorder_delete(const char *name){return request(CMD_DELETE,name);}
void capsule_recorder_snapshot(capsule_snapshot_t *snapshot) {
    portENTER_CRITICAL(&s_lock);*snapshot=s_snapshot;portEXIT_CRITICAL(&s_lock);
}
