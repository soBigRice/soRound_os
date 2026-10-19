// Run the actual output worker; substitute only scheduling and the I2S/codec boundary.
#include "audio_sdk.h"
#include <stdbool.h>
#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static FILE *test_fopen(const char *path,const char *mode);
#define fopen test_fopen
#include "../main/audio_out.c"
#undef fopen
static jmp_buf stopped;
static bool muted,check_idle=false,resume_from_idle;
static unsigned scene_chunks,alarm_samples,wav_samples,scenario;
static bool missing;
static unsigned chunks,samples,acquired,released,opened,closed,idle_checks;
static const audio_codec_data_if_t fake_if={0};
static i2s_chan_config_t tx_config;
static int16_t dma_ring[6*240],pcm_written[16000*5];
static unsigned dma_at;
uint8_t settings_silent(void){return muted;}
uint8_t settings_volume(void){return 48;}
bool audio_bus_acquire(TickType_t wait){assert(wait==50);++acquired;return true;}
void audio_bus_release(void){++released;}
void power_audio_on(void){}
void *board_i2c_bus(void){return (void *)1;}
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle){
    assert(fn==output_worker&&!strcmp(name,"aout")&&stack==4096&&!arg&&priority==5);*(TaskHandle_t *)handle=(void *)1;return pdPASS;
}
void vTaskDelete(void *task){assert(!task);longjmp(stopped,1);}
void vTaskDelay(unsigned ms){assert(ms==40);}
void xTaskNotifyGive(TaskHandle_t task){assert(task);}
unsigned ulTaskNotifyTake(int clear,unsigned timeout){
    assert(clear==pdTRUE);
    if(!timeout)return 0;
    assert(timeout==portMAX_DELAY);
    assert(!s_req&&!s_busy&&!s_playing_req);
    // The page stays open: DMA continues cycling while the worker waits for the next tap.
    if(check_idle&&s_dev) {
        unsigned ring=tx_config.dma_desc_num*tx_config.dma_frame_num;
        assert(ring==sizeof dma_ring/sizeof dma_ring[0]);
        for(unsigned pass=0;pass<2;++pass)for(unsigned i=0;i<ring;++i) {
            if(pass==1)assert(dma_ring[i]==0&&"Idle I2S must not replay a finished sound");
            if(tx_config.auto_clear_after_cb||tx_config.auto_clear_before_cb)dma_ring[i]=0;
        }
        ++idle_checks;
        if(resume_from_idle){resume_from_idle=false;audio_out_knock();return 1;}
    }
    audio_out_deinit();return 1;
}
void esp_log_level_set(const char *tag,int level){(void)tag;(void)level;}
int i2s_new_channel(const i2s_chan_config_t *cfg,i2s_chan_handle_t *out,void *rx){tx_config=*cfg;memset(dma_ring,0,sizeof dma_ring);dma_at=0;assert(!rx);*out=(void *)1;return 0;}
int i2s_channel_init_std_mode(i2s_chan_handle_t tx,const i2s_std_config_t *cfg){assert(tx&&cfg->gpio_cfg.dout==8);return 0;}
int i2s_del_channel(i2s_chan_handle_t tx){assert(tx);return 0;}
const audio_codec_data_if_t *audio_codec_new_i2s_data(const audio_codec_i2s_cfg_t *cfg){assert(cfg->tx_handle&&!cfg->rx_handle);return &fake_if;}
const audio_codec_ctrl_if_t *audio_codec_new_i2c_ctrl(const audio_codec_i2c_cfg_t *cfg){assert(cfg->bus_handle);return &fake_if;}
const audio_codec_gpio_if_t *audio_codec_new_gpio(void){return &fake_if;}
const audio_codec_if_t *es8311_codec_new(const es8311_codec_cfg_t *cfg){assert(cfg->use_mclk&&cfg->pa_pin==46);return &fake_if;}
esp_codec_dev_handle_t esp_codec_dev_new(const esp_codec_dev_cfg_t *cfg){assert(cfg->dev_type==1);return (void *)1;}
int esp_codec_dev_open(esp_codec_dev_handle_t dev,const esp_codec_dev_sample_info_t *fs){assert(dev&&fs->sample_rate==16000&&fs->channel==1&&fs->bits_per_sample==16);++opened;return 0;}
int esp_codec_dev_close(esp_codec_dev_handle_t dev){assert(dev);++closed;return 0;}
void esp_codec_dev_delete(esp_codec_dev_handle_t dev){assert(dev);}
void audio_codec_delete_codec_if(const audio_codec_if_t *p){assert(p);}
void audio_codec_delete_ctrl_if(const audio_codec_ctrl_if_t *p){assert(p);}
void audio_codec_delete_gpio_if(const audio_codec_gpio_if_t *p){assert(p);}
void audio_codec_delete_data_if(const audio_codec_data_if_t *p){assert(p);}
int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t dev,unsigned volume){assert(dev&&volume==48);return 0;}
int esp_codec_dev_write(esp_codec_dev_handle_t dev,void *buffer,int bytes){
    assert(dev);++chunks;unsigned count=(unsigned)bytes/2;
    assert(samples+count<=sizeof pcm_written/sizeof pcm_written[0]);
    int16_t *pcm=buffer;
    memcpy(pcm_written+samples,buffer,(size_t)bytes);
    for(unsigned i=0;i<count;++i){dma_ring[dma_at]=pcm[i];dma_at=(dma_at+1)%(sizeof dma_ring/sizeof dma_ring[0]);}
    samples+=count;
    if(s_playing_req==4) {
        ++scene_chunks;
        if(scenario==1 && scene_chunks==1){audio_out_alarm();assert(s_req==2);}
        if(scenario==2 && scene_chunks==3)audio_out_scene_stop();
    } else if(s_playing_req==2) {
        alarm_samples+=count;
        if(scenario==1 && alarm_samples==count)audio_out_scene_stop();
    } else if(s_playing_req==5) {
        wav_samples+=count;
        if(scenario==3 && wav_samples==count){audio_out_alarm();assert(s_req==2);}
        if(scenario==7 && wav_samples==count){audio_out_deinit();assert(audio_out_wav_state()==AUDIO_WAV_PLAYING);}
    }
    return 0;
}

static FILE *test_fopen(const char *path,const char *mode) {
    assert(!strcmp(path,"/caps/1234ABCD.WAV")&&!strcmp(mode,"rb"));if(missing)return NULL;
    FILE *f=tmpfile();assert(f);uint8_t h[44];capsule_wav_header(h,32000);assert(fwrite(h,1,44,f)==44);
    for(unsigned i=0;i<16000;++i){int16_t sample=(int16_t)(i%4000-2000);assert(fwrite(&sample,2,1,f)==1);}rewind(f);return f;
}
static void run(void) {
    chunks=samples=scene_chunks=alarm_samples=wav_samples=0;
    if(!setjmp(stopped))output_worker(NULL);
    assert(!s_worker&&!s_dev&&!s_tx&&acquired==released&&opened==closed);
}
int main(void) {
    assert(!audio_out_wav_start("/img/1234ABCD.WAV"));assert(!audio_out_wav_start("/caps/../x.WAV"));
    scenario=1;assert(audio_out_scene_start(AUDIO_SCENE_RAIN));assert(audio_out_scene_active());run();
    assert(scene_chunks==1 && alarm_samples==20800 && !audio_out_scene_active());
    scenario=2;assert(audio_out_scene_start(AUDIO_SCENE_OCEAN));run();assert(scene_chunks==3);
    assert(audio_out_scene_start(AUDIO_SCENE_FIRE));run();assert(scene_chunks==3);
    scenario=3;assert(audio_out_wav_start("/caps/1234ABCD.WAV"));run();
    assert(wav_samples==512 && alarm_samples==20800 && audio_out_wav_state()==AUDIO_WAV_CANCELLED);
    scenario=4;assert(audio_out_wav_start("/caps/1234ABCD.WAV"));run();
    assert(wav_samples==16000 && audio_out_wav_state()==AUDIO_WAV_DONE);
    assert(audio_out_alarm_once());audio_out_scene_stop();run();assert(alarm_samples==20800);
    assert(audio_out_wav_start("/caps/1234ABCD.WAV"));assert(audio_out_alarm_once());run();
    assert(alarm_samples==20800 && audio_out_wav_state()==AUDIO_WAV_CANCELLED);
    scenario=7;assert(audio_out_wav_start("/caps/1234ABCD.WAV"));run();assert(wav_samples==512 && audio_out_wav_state()==AUDIO_WAV_CANCELLED);
    missing=true;assert(audio_out_wav_start("/caps/1234ABCD.WAV"));run();assert(audio_out_wav_state()==AUDIO_WAV_FAILED);missing=false;
    assert(audio_out_wav_start("/caps/1234ABCD.WAV"));muted=true;run();assert(audio_out_wav_state()==AUDIO_WAV_CANCELLED);
    assert(!audio_out_scene_start(AUDIO_SCENE_RAIN));muted=false;
    puts("capsule audio: real shared worker, three ambience modes, alarm preemption, scene stop preserves alarm, bounded WAV playback/cancellation, missing file and mute passed");
}
