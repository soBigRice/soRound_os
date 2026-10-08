// Real LVGL renderer and watchface controller; time/network/power/image services are fixtures.
#include "watchface_ui.h"
#include "src/misc/lv_timer_private.h"
#include "power.h"
#include "settings.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static time_t now;
static time_t fixture_time(time_t *out){if(out)*out=now;return now;}
static bool ip_ready=true;
static bool network=true,battery_ok=true,weather_ok=true,loading;
bool wifi_service_ready(void){return network && ip_ready;}
static int low=21,high=28,temperature=26,humidity=64,code=3,soc=74;
static int weather_polls,image_reads;
static char ssid[33]="soRound";
static uint16_t image_pixels[3][466*466];
static lv_image_dsc_t images[3];
static const lv_image_dsc_t *image;
uint8_t settings_face(void){return 4;}
uint8_t settings_lang(void){return 0;}
void quickpanel_hide(void){}
bool power_read(int *value,pwr_state_t *state){*value=soc;*state=PWR_DISCHARGING;return battery_ok;}
int esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap){memcpy(ap->ssid,ssid,33);return network?ESP_OK:-1;}
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key){(void)key;return (void *)1;}
int esp_netif_get_ip_info(esp_netif_t *n,esp_netif_ip_info_t *ip){(void)n;ip->ip.addr=0x1801a8c0;return ESP_OK;}
void weather_poll(void){++weather_polls;}
bool weather_cached(int *temp,int *lo,int *hi,int *weather_code,int *hum){*temp=temperature;*lo=low;*hi=high;*weather_code=code;*hum=humidity;return weather_ok;}
const lv_image_dsc_t *img_store_face_image_for(int theme){assert(theme>=0&&theme<3);++image_reads;return image?image:loading?NULL:&images[theme];}
bool img_store_face_loading(int theme){(void)theme;return loading;}
#define time(out) fixture_time(out)
#include "../../main/watchface.c"
#undef time
static uint16_t pixels[466*466];
_Alignas(LV_DRAW_BUF_ALIGN) static uint16_t buffer[466*48];
static unsigned flushed;
static lv_display_t *display;
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *bytes){int w=lv_area_get_width(a);for(int y=a->y1;y<=a->y2;++y){memcpy(pixels+y*466+a->x1,bytes,(size_t)w*2);bytes+=w*2;flushed+=(unsigned)w;}lv_display_flush_ready(d);}
static uint64_t capture(const char *folder,const char *name){
    lv_obj_update_layout(lv_layer_top());lv_refr_now(display);uint64_t hash=1469598103934665603ULL;
    for(unsigned i=0;i<466*466;++i){hash^=pixels[i];hash*=1099511628211ULL;}
    if(folder){char path[1024];snprintf(path,sizeof path,"%s/%s.ppm",folder,name);FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n466 466\n255\n");
        for(unsigned i=0;i<466*466;++i){uint16_t p=pixels[i];uint8_t rgb[]={((p>>11)&31)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};fwrite(rgb,1,3,f);}assert(fclose(f)==0);}
    return hash;
}
static uint64_t digest_value(uint64_t hash,uint32_t value){
    for(unsigned i=0;i<4;++i){hash^=(value>>(i*8))&255;hash*=1099511628211ULL;}
    return hash;
}
static void font_bitmap_digests(bool snapshot){
    const lv_font_t *fonts[]={&font_wf_7,&font_wf_9,&font_wf_14,&font_wf_18,&font_wf_26,
        &font_wf_36,&font_wf_52,&font_wf_56,&font_wf_72,&font_wf_82,&font_wf_104,&font_wf_112,&font_wf_164};
    // Captured from the original uncompressed 4bpp fonts before changing storage.
    const uint64_t expected[]={0xe8502c903d939f2fULL,0xd98563813adeb9e8ULL,0x374b65aacc033064ULL,
        0xc48c5aba5bdbedffULL,0xaafce55160dbcc46ULL,0x210958387de7b1beULL,0xed76339861c8bc4fULL,
        0x365d9eb1f9b560aeULL,0x360e4e275f42ed35ULL,0xfd4dc8c44891142bULL,0xe156489dcb1b2942ULL,
        0xa11af7c4d1438f5fULL,0xe3b2024bfe45c995ULL};
    for(unsigned i=0;i<sizeof fonts/sizeof fonts[0];++i){
        const lv_font_t *font=fonts[i];const lv_font_fmt_txt_dsc_t *format=font->dsc;
        assert(format->cmap_num==1);const lv_font_fmt_txt_cmap_t *cmap=format->cmaps;
        uint64_t hash=digest_value(1469598103934665603ULL,font->line_height);
        hash=digest_value(hash,font->base_line);
        for(unsigned j=0;j<cmap->list_length;++j){
            uint32_t code=cmap->range_start+cmap->unicode_list[j];lv_font_glyph_dsc_t glyph={0};
            assert(lv_font_get_glyph_dsc(font,&glyph,code,0));
            hash=digest_value(hash,code);hash=digest_value(hash,glyph.adv_w);
            hash=digest_value(hash,glyph.box_w);hash=digest_value(hash,glyph.box_h);
            hash=digest_value(hash,(uint32_t)glyph.ofs_x);hash=digest_value(hash,(uint32_t)glyph.ofs_y);
            if(!glyph.box_w||!glyph.box_h)continue;
            lv_draw_buf_t *buf=lv_draw_buf_create(glyph.box_w,glyph.box_h,LV_COLOR_FORMAT_A8,0);assert(buf);
            assert(lv_font_get_glyph_bitmap(&glyph,buf));
            for(unsigned y=0;y<glyph.box_h;++y)for(unsigned x=0;x<glyph.box_w;++x){
                hash^=buf->data[y*buf->header.stride+x];hash*=1099511628211ULL;
            }
            lv_font_glyph_release_draw_data(&glyph);lv_draw_buf_destroy(buf);
        }
        if(snapshot)printf("font[%u]=0x%016llx\n",i,(unsigned long long)hash);
        else assert(hash==expected[i]);
    }
}
int main(int argc,char **argv){
    if(argc==2&&strcmp(argv[1],"--font-digests")==0){lv_init();font_bitmap_digests(true);lv_deinit();return 0;}
    const char *folder=argc>1?argv[1]:NULL;
    setenv("TZ","UTC",1);tzset();struct tm t={.tm_year=126,.tm_mon=9,.tm_mday=5,.tm_hour=10,.tm_min=8,.tm_sec=21};now=mktime(&t);
    ip_ready=false;snapshot(true);assert(!s_data.wifi && !s_data.ip[0]);ip_ready=true;snapshot(true);assert(s_data.wifi);
    for(int i=0;i<3;++i){images[i]=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=466,.h=466,.stride=932},.data_size=sizeof image_pixels[i],.data=(uint8_t *)image_pixels[i]};
        if(argc>2){char path[1024];snprintf(path,sizeof path,"%s/%s.rgb565",argv[2],watchface_theme_name(i));FILE *f=fopen(path,"rb");assert(f);assert(fread(image_pixels[i],1,sizeof image_pixels[i],f)==sizeof image_pixels[i]);assert(fclose(f)==0);}}
    lv_init();font_bitmap_digests(false);i18n_init();display=lv_display_create(466,466);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,buffer,NULL,sizeof buffer,LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
    lv_obj_set_style_bg_color(lv_screen_active(),lv_color_black(),0);
    watchface_init();assert(watchface_count()==21&&watchface_selected()==4&&!watchface_visible());watchface_show();assert(wf_timer);
    uint64_t hashes[21];
    for(int i=0;i<21;++i){watchface_select(i);char name[32];snprintf(name,sizeof name,"face-%02d",i);hashes[i]=capture(folder,name);for(int j=0;j<i;++j)assert(hashes[i]!=hashes[j]);assert(lv_obj_get_child_count(wf_content)==0);}
    // New hands must refresh every second without being mistaken for weather/image kinds.
    int polls=weather_polls,reads=image_reads;
    for(int i=15;i<21;++i){watchface_select(i);uint64_t before=capture(NULL,"hands-before");flushed=0;++now;tick(NULL);assert(before!=capture(NULL,"hands-after")&&flushed>0);--now;}
    assert(weather_polls==polls&&image_reads==reads);
    assert(watchface_theme_for(20)==3&&!strcmp(watchface_kind_name(20),"dots"));
    assert(watchface_index_in_theme(1,2)==7&&watchface_index_in_theme(3,14)==15&&watchface_index_in_theme(3,20)==20&&watchface_index_in_theme(2,20)==10);
    // At 10:08, minute progress must leave six o'clock dark; the hour ring must have passed it.
    watchface_select(12);capture(NULL,"ring-semantics");
    // Between the 30/31 minute dots, the unused outer track must be black.
    assert(((pixels[442*466+244]>>5)&63)<=13);
    assert(((pixels[398*466+233]>>5)&63)>15);
    // The unused portion is dotted, not a duplicate complete solid ring.
    assert(pixels[85*466+153]==0);
    for(int i=0;i<21;++i){watchface_select(i);watchface_set_aod(true);char name[32];snprintf(name,sizeof name,"aod-%02d",i);uint64_t hash=capture(folder,name);
        assert(wf_timer->period==39000);now+=9;tick(NULL);assert(hash==capture(NULL,"same-minute"));now-=9;watchface_set_aod(false);assert(wf_timer->period==1000);}
    watchface_select(3);capture(NULL,"before");flushed=0;low=18;high=34;tick(NULL);capture(folder,"weather-range-change");assert(flushed>0&&s_data.low==18&&s_data.high==34);
    weather_ok=false;tick(NULL);capture(folder,"weather-unavailable");assert(!s_data.weather_valid);weather_ok=true;
    temperature=-12;low=-18;high=-2;humidity=100;code=71;tick(NULL);capture(folder,"weather-negative");
    watchface_select(0);network=false;battery_ok=false;snapshot(true);lv_obj_invalidate(wf_content);capture(folder,"network-power-unavailable");assert(!s_data.wifi&&!s_data.battery_valid&&!s_data.ip[0]);
    network=true;battery_ok=true;soc=0;memset(ssid,'W',32);ssid[32]=0;snapshot(true);lv_obj_invalidate(wf_content);capture(folder,"ssid-32-low-battery");assert(strlen(s_data.ssid)==32&&s_data.battery==0);
    for(int theme=0;theme<3;++theme) {
        watchface_select(theme*5);char name[32];snprintf(name,sizeof name,"ssid-32-theme-%d",theme);
        capture(folder,name);assert(strlen(s_data.ssid)==32&&s_data.battery==0);
    }
    watchface_select(14);loading=true;image=NULL;snapshot(true);lv_obj_invalidate(wf_content);capture(folder,"image-loading");assert(s_data.image_loading&&!s_data.image);
    image=&images[0];loading=false;tick(NULL);capture(folder,"custom-image");assert(s_data.image==image);
    for(unsigned i=0;i<466*466;++i)image_pixels[0][i]=0xffff;
    lv_obj_invalidate(wf_content);capture(folder,"custom-image-bright");assert((pixels[80*466+233]&31)<=12);
    watchface_set_sleep(true);assert(!wf_timer&&watchface_visible());now+=3660;watchface_set_sleep(false);assert(wf_timer&&s_data.time.tm_hour==11&&s_data.time.tm_min==9);
    watchface_select(-1);assert(watchface_selected()==0);watchface_select(100);assert(watchface_selected()==20);
    watchface_hide();assert(!wf_timer&&!watchface_visible());watchface_select(9);watchface_show();assert(watchface_selected()==9);
    watchface_hide();lv_deinit();puts("21 distinct native faces, six hands/second updates, catalogue boundaries, AOD/minute scheduling, sleep/wake, selection, weather ranges/failure, 32-byte SSID, unavailable battery, image loading/custom priority passed.");return 0;
}
