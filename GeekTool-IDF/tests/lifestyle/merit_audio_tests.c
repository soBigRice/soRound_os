// Run the actual output worker; substitute only scheduling and the I2S/codec boundary.
#include "audio_sdk.h"
#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../main/audio_out.c"
static jmp_buf stopped;
static bool muted,retrigger,stop_during_write,fail_write,check_idle=true,resume_from_idle;
static unsigned chunks,samples,acquired,released,opened,closed,restarts,idle_checks,burst_chunks;
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
    if(check_idle&&s_dev&&!fail_write) {
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
    if(s_playing_req==3) {
        assert(bytes<=256);
        if(chunks==3&&retrigger){
            int16_t continuing;merit_sound_fill(&continuing,256,1);
            assert(pcm[0]==continuing&&"Retrigger must preserve the ringing waveform at the boundary");++restarts;
        }
        if(burst_chunks&&chunks>1&&chunks<=burst_chunks+1) {
            int16_t continuing;merit_sound_fill(&continuing,128,1);
            assert(pcm[0]==continuing&&"Every rapid retrigger must preserve continuity");
        }
        if(chunks==2&&retrigger){audio_out_knock();assert(s_req==3);}
        if(chunks<=burst_chunks){for(unsigned i=0;i<20;++i)audio_out_knock();assert(s_req==3);}
    } else {audio_out_knock();assert(s_req==0);} // A wood tap must not preempt alarms or volume preview.
    if(chunks==2&&stop_during_write)audio_out_deinit();
    return fail_write?-1:0;
}
static void pump(int request){chunks=samples=idle_checks=0;audio_out_init();if(request==3)audio_out_knock();else if(request==2)audio_out_alarm();else audio_out_blip();if(!setjmp(stopped))output_worker(NULL);assert(!s_worker&&!s_dev&&!s_tx&&acquired==released&&opened==closed);}
static void wav(const char *path){
    int16_t pcm[MERIT_SOUND_SAMPLES];merit_sound_fill(pcm,0,MERIT_SOUND_SAMPLES);
    FILE *f=fopen(path,"wb");assert(f);
    const unsigned hits=4,gap=MERIT_SOUND_RATE/2,total=hits*gap,bytes=total*2;
    uint32_t header[]={36+bytes,16,MERIT_SOUND_RATE,MERIT_SOUND_RATE*2,bytes};
    uint16_t format[]={1,1,2,16};
    assert(fwrite("RIFF",1,4,f)==4);fwrite(header,4,1,f);fwrite("WAVEfmt ",1,8,f);fwrite(header+1,4,1,f);
    fwrite(format,2,2,f);fwrite(header+2,4,2,f);fwrite(format+2,2,2,f);fwrite("data",1,4,f);fwrite(header+4,4,1,f);
    for(unsigned i=0;i<total;++i){int16_t p=i%gap<MERIT_SOUND_SAMPLES?pcm[i%gap]:0;fwrite(&p,2,1,f);}assert(!fclose(f));
}
int main(int argc,char **argv){
    if(argc>1&&!strcmp(argv[1],"--idle-dma")){pump(3);assert(idle_checks==1);return 0;}
    if(argc>1&&!strcmp(argv[1],"--retrigger-boundary")){check_idle=false;retrigger=true;pump(3);assert(restarts==1);return 0;}
    int16_t pcm[MERIT_SOUND_SAMPLES];merit_sound_fill(pcm,0,MERIT_SOUND_SAMPLES);assert(pcm[0]==0&&pcm[MERIT_SOUND_SAMPLES-1]==0);
    double attack=0,tail=0;int peak=0;for(unsigned i=0;i<MERIT_SOUND_SAMPLES;++i){int v=abs(pcm[i]);if(v>peak)peak=v;if(i<320)attack+=(double)v*v;if(i>=MERIT_SOUND_SAMPLES-320)tail+=(double)v*v;}assert(peak>12000&&peak<32767&&tail<attack*.001);
    pump(3);assert(samples==MERIT_SOUND_SAMPLES&&idle_checks==1);
    retrigger=true;pump(3);assert(restarts==1&&samples==MERIT_SOUND_SAMPLES+256);retrigger=false;
    burst_chunks=200;pump(3);assert(samples==burst_chunks*128+MERIT_SOUND_SAMPLES&&idle_checks==1);burst_chunks=0;
    resume_from_idle=true;pump(3);assert(samples==2*MERIT_SOUND_SAMPLES&&idle_checks==2);
    stop_during_write=true;pump(3);assert(chunks==2);stop_during_write=false;
    fail_write=true;pump(3);assert(chunks==1);fail_write=false;
    pump(2);assert(samples==RATE*1300/1000);pump(1);assert(samples==RATE*80/1000);
    muted=true;pump(3);assert(!samples);muted=false;audio_out_knock();assert(!s_req);
    if(argc>1)wav(argv[1]);puts("merit audio: actual worker PCM, idle DMA silence, 4000-tap burst stop, retrigger continuity, idle resume, cancellation/I2S failure cleanup, mute/volume, preserved alarm and preview passed");
}
