// Real LVGL renderer and watchface controller; time/network/power/image services are fixtures.
#include "watchface_ui.h"
#include "watchface_type_fonts.h"
#include "watchface_studio_fonts.h"
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
static uint8_t restored_face=4;
static char ssid[33]="soRound";
static uint16_t image_pixels[3][466*466];
static lv_image_dsc_t images[3];
static const lv_image_dsc_t *image;
uint8_t settings_face(void){return restored_face;}
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
_Alignas(LV_DRAW_BUF_ALIGN) static uint16_t second_buffer[466*40];
_Alignas(LV_DRAW_BUF_ALIGN) static uint16_t full_buffer[466*466];
static uint16_t reference_pixels[466*466];
static bool device_format;
static unsigned flushed;
static lv_display_t *display;
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *bytes){int w=lv_area_get_width(a);for(int y=a->y1;y<=a->y2;++y){
    if(device_format)for(int x=0;x<w;x++)pixels[y*466+a->x1+x]=(uint16_t)((bytes[x*2]<<8)|bytes[x*2+1]);
    else memcpy(pixels+y*466+a->x1,bytes,(size_t)w*2);
    bytes+=w*2;flushed+=(unsigned)w;}lv_display_flush_ready(d);}
static uint64_t capture(const char *folder,const char *name){
    lv_obj_update_layout(lv_layer_top());lv_refr_now(display);uint64_t hash=1469598103934665603ULL;
    for(unsigned i=0;i<466*466;++i){hash^=pixels[i];hash*=1099511628211ULL;}
    if(folder){char path[1024];snprintf(path,sizeof path,"%s/%s.ppm",folder,name);FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n466 466\n255\n");
        for(unsigned i=0;i<466*466;++i){uint16_t p=pixels[i];uint8_t rgb[]={((p>>11)&31)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};fwrite(rgb,1,3,f);}assert(fclose(f)==0);}
    return hash;
}
static void capture_studio_references(const char *folder){
    if(!folder)return;
    // 设计审阅使用固定日期，原有 21 款的默认时间夹具保持不变。
    // Export designs for review at a fixed date without changing the original 21-face fixture.
    time_t saved_now=now;int saved_index=watchface_selected();bool saved_aod=s_aod;
    struct tm reference={.tm_year=126,.tm_mon=9,.tm_mday=20,.tm_hour=10,.tm_min=8,.tm_sec=36};
    now=mktime(&reference);watchface_set_aod(false);
    for(int index=21;index<WATCHFACE_COUNT;++index){
        char name[64];watchface_select(index);snprintf(name,sizeof name,"studio-reference-%02d",index);
        capture(folder,name);watchface_set_aod(true);
        snprintf(name,sizeof name,"studio-reference-aod-%02d",index);capture(folder,name);
        watchface_set_aod(false);
    }
    // 新款额外保留午夜与日末实图，检查0/23小时和59分钟的实际构图。
    // Retain midnight and end-of-day frames to inspect real layouts at 00:00 and 23:59.
    const int hours[]={0,23},minutes[]={0,59},seconds[]={0,59};
    for(int state=0;state<2;++state){
        struct tm boundary=reference;boundary.tm_hour=hours[state];boundary.tm_min=minutes[state];boundary.tm_sec=seconds[state];
        now=mktime(&boundary);
        for(int index=36;index<WATCHFACE_COUNT;++index){
            char name[64];watchface_select(index);
            snprintf(name,sizeof name,"atelier-%02d-%s",index,state?"late":"midnight");capture(folder,name);
        }
    }
    now=saved_now;watchface_set_aod(saved_aod);watchface_select(saved_index);
    capture(NULL,"restore-after-studio-reference");
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
static void studio_font_contract(void){
    // 新款必须命中成对字号，防止233px预览静默退到14px默认字库。
    // New faces must resolve paired sizes rather than silently using the 14px preview fallback.
    assert(wfs_studio_font(30,true,false)==&font_studio_regular_15);
    assert(wfs_studio_font(96,true,true)==&font_studio_semibold_48);
    assert(wfs_studio_font(144,true,true)==&font_studio_semibold_72);
    const int regular[]={8,9,10,11,12,13,14,15,30,72,86,87,91,96,99};
    const int semibold[]={12,13,14,94,96,144,146};
    for(int heavy=0;heavy<2;heavy++){
        const int *sizes=heavy?semibold:regular;unsigned count=heavy?sizeof semibold/sizeof semibold[0]:sizeof regular/sizeof regular[0];
        for(unsigned i=0;i<count;i++)for(int preview=0;preview<2;preview++){
            const lv_font_t *font=wfs_studio_font(sizes[i],preview,heavy);bool small=sizes[i]<=15;
            // 检查真实解析字体，不允许fallback掩盖冒号或小字缺失。
            // Check the resolved face so fallback cannot conceal a missing colon or caption glyph.
            for(uint32_t code=small?32u:48u;code<=(small?126u:58u);code++){
                lv_font_glyph_dsc_t glyph={0};assert(lv_font_get_glyph_dsc(font,&glyph,code,0));
                assert(!glyph.is_placeholder && glyph.resolved_font==font);
            }
            if(small){lv_font_glyph_dsc_t glyph={0};assert(lv_font_get_glyph_dsc(font,&glyph,0xb7,0));assert(!glyph.is_placeholder && glyph.resolved_font==font);}
        }
    }
}
static void typography_font_contract(void){
    // 每个大字尺寸必须有真实字形与对应半尺寸，不允许静默回退。
    // Every oversized face and paired preview must resolve real glyphs without silent fallback.
    const struct {int size;wft_weight_t weight;} pairs[]={{161,WFT_REGULAR},{166,WFT_REGULAR},{176,WFT_REGULAR},
        {166,WFT_MEDIUM},{215,WFT_MEDIUM},{200,WFT_BLACK},{202,WFT_BLACK},{220,WFT_BLACK}};
    assert(wft_font(202,true,WFT_BLACK)==&font_type_black_101);
    assert(wft_font(161,true,WFT_REGULAR)==&font_type_regular_81);
    assert(wft_font(215,true,WFT_MEDIUM)==&font_type_semibold_108);
    assert(wft_font(13,false,WFT_BLACK)==NULL);
    for(unsigned i=0;i<sizeof pairs/sizeof pairs[0];i++)for(int preview=0;preview<2;preview++){
        const lv_font_t *font=wft_font(pairs[i].size,preview,pairs[i].weight);assert(font);
        for(uint32_t code='0';code<=':';code++){
            lv_font_glyph_dsc_t glyph={0};assert(lv_font_get_glyph_dsc(font,&glyph,code,0));
            assert(!glyph.is_placeholder && glyph.resolved_font==font);
        }
    }
}
static void catalogue_contract(void){
    // 旧 NVS ID 必须逐一保持原语义；新增款式只能追加到末尾。
    // Preserve every persisted ID; append new faces only at the end.
    const char *const preserved[]={
        "TYPE / dots","TYPE / bold","TYPE / rings","TYPE / weather","TYPE / image",
        "ORBIT / dots","ORBIT / bold","ORBIT / rings","ORBIT / weather","ORBIT / image",
        "SHIFT / dots","SHIFT / bold","SHIFT / rings","SHIFT / weather","SHIFT / image",
        "HAND / mark","HAND / arc","HAND / numeral","HAND / orbit","HAND / frame","HAND / dots"};
    const char *const additions[]={"radar","tape","iris","maze","fold","pixel","horizon","tide","growth","sector","solar","grid","petal","ribbon","binary","mono","bauhaus","seven","vinyl","prism","metro","moon-gate","orrery","stack","air","offset","column","cut","edition","activity","circuit","matrix","vector","duo","stride"};
    assert(watchface_count()==56 && WATCHFACE_THEME_COUNT==5);
    for(int i=0;i<21;++i){
        assert(!strcmp(watchface_name(i),preserved[i]));
        assert(watchface_theme_for(i)==(i<15?i/5:3));
    }
    assert(!strcmp(watchface_theme_name(4),"STUDIO"));
    for(int i=0;i<35;++i){
        char name[32];snprintf(name,sizeof name,"STUDIO / %s",additions[i]);
        assert(!strcmp(watchface_name(21+i),name));
        assert(!strcmp(watchface_kind_name(21+i),additions[i]));
        assert(watchface_theme_for(21+i)==4);
        assert(watchface_index_in_theme(4,21+i)==21+i);
    }
    assert(watchface_index_in_theme(1,2)==7 && watchface_index_in_theme(3,14)==15);
    assert(watchface_index_in_theme(3,20)==20 && watchface_index_in_theme(2,20)==10);
    assert(watchface_index_in_theme(4,20)==21 && watchface_index_in_theme(3,29)==15);
    assert(watchface_index_in_theme(0,29)==0 && watchface_index_in_theme(2,29)==10);
    assert(watchface_theme_for(-1)==-1 && watchface_theme_for(WATCHFACE_COUNT)==-1);
    assert(!watchface_name(-1)[0] && !watchface_name(WATCHFACE_COUNT)[0]);
    assert(!watchface_kind_name(-1)[0] && !watchface_kind_name(WATCHFACE_COUNT)[0]);
    assert(!watchface_theme_name(-1)[0] && !watchface_theme_name(5)[0]);
    assert(watchface_index_in_theme(-1,29)==29 && watchface_index_in_theme(5,29)==29);
}
static uint16_t expected_color(uint32_t rgb){return (uint16_t)(((rgb>>8)&0xf800)|((rgb>>5)&0x07e0)|((rgb>>3)&0x001f));}
static void sport_time_contract(void){
    // 在06:30:15验证图形真实编码小时、分钟、秒，不能只是随秒变化的装饰。
    // At 06:30:15 verify actual hour/minute/second encodings, not merely decoration that changes every second.
    time_t saved=now;int selected=watchface_selected();bool aod=s_aod;
    struct tm clock={.tm_year=126,.tm_mon=9,.tm_mday=22,.tm_hour=6,.tm_min=30,.tm_sec=15};
    now=mktime(&clock);watchface_set_aod(false);watchface_select(50);capture(NULL,"activity-semantics");
    assert(pixels[437*466+233]==expected_color(0xf16b8d));
    assert(pixels[233*466+29]==expected_color(0x341b26));
    assert(pixels[233*466+406]==expected_color(0xb8e95c));
    assert(pixels[233*466+60]==expected_color(0x26331b));
    assert(pixels[233*466+375]==expected_color(0x66cde3));
    assert(pixels[375*466+233]==expected_color(0x182d35));
    watchface_select(52);capture(NULL,"matrix-semantics");
    assert(pixels[108*466+219]==expected_color(0xe6ad73));
    assert(pixels[108*466+246]==expected_color(0x2e251e));
    assert(pixels[333*466+345]==expected_color(0x9fd1b7));
    assert(pixels[355*466+120]==expected_color(0x1e3029));
    watchface_select(55);capture(NULL,"stride-semantics");
    assert(pixels[156*466+120]==expected_color(0xed6b91));
    assert(pixels[156*466+200]==expected_color(0x321d27));
    assert(pixels[270*466+200]==expected_color(0x79c9e6));
    assert(pixels[270*466+300]==expected_color(0x1a2c36));
    assert(pixels[384*466+120]==expected_color(0xc6e97c));
    assert(pixels[384*466+200]==expected_color(0x2a321d));
    now=saved;watchface_set_aod(aod);watchface_select(selected);capture(NULL,"restore-after-sport-semantics");
}
static void studio_tile_contract(void){
    // 相同真实绘制器必须在整帧与40行双缓冲中输出相同像素，包括偏移预览和常显。
    // The real renderer must match full-frame pixels in 40-row double buffers, including offset previews and AOD.
    const int hours[]={0,10,12,23},minutes[]={0,8,34,59},seconds[]={0,15,30,59};
    watchface_hide();
    for(int format=0;format<2;format++){
        device_format=format;
        lv_display_set_color_format(display,format?LV_COLOR_FORMAT_RGB565_SWAPPED:LV_COLOR_FORMAT_RGB565);
        for(int small=0;small<2;small++)for(int index=21;index<WATCHFACE_COUNT;index++){
            s_idx=index;lv_obj_t *view=surface(lv_screen_active(),small?233:466,small?(0x100u|(uintptr_t)index):0);
            for(int aod=0;aod<2;aod++)for(int t=0;t<4;t++){
                struct tm time={.tm_year=126,.tm_mon=9,.tm_mday=20+t,.tm_hour=hours[t],.tm_min=minutes[t],.tm_sec=seconds[t]};
                now=mktime(&time);s_aod=aod;snapshot(true);
                lv_display_set_buffers(display,full_buffer,NULL,sizeof full_buffer,LV_DISPLAY_RENDER_MODE_PARTIAL);
                lv_obj_invalidate(lv_screen_active());lv_refr_now(display);memcpy(reference_pixels,pixels,sizeof pixels);
                lv_display_set_buffers(display,buffer,second_buffer,466*40*2,LV_DISPLAY_RENDER_MODE_PARTIAL);
                lv_obj_invalidate(lv_screen_active());lv_refr_now(display);
                if(memcmp(reference_pixels,pixels,sizeof pixels))fprintf(stderr,"tile mismatch: format=%d preview=%d face=%d aod=%d time=%d\n",format,small,index,aod,t);
                assert(!memcmp(reference_pixels,pixels,sizeof pixels));
                // 清掉屏幕中的窄区域再局部恢复，覆盖横向裁剪与非整块起止边界。
                // Clear and restore a narrow screen region to cover horizontal clips and non-tile-aligned edges.
                lv_area_t coords;lv_obj_get_coords(view,&coords);int size=small?233:466;
                lv_area_t dirty={coords.x1+size/3,coords.y1+size/5,coords.x1+size*2/3,coords.y1+size*4/5};
                for(int y=dirty.y1;y<=dirty.y2;y++)for(int x=dirty.x1;x<=dirty.x2;x++)pixels[y*466+x]^=0xffff;
                lv_obj_invalidate_area(view,&dirty);lv_refr_now(display);
                assert(!memcmp(reference_pixels,pixels,sizeof pixels));
            }
            lv_obj_delete(view);
        }
    }
    printf("STUDIO: %d full-frame/40-row pairs matched in both RGB565 formats, 466/233px, AOD and boundary times.\n",WATCHFACE_STUDIO_COUNT*32);
}
int main(int argc,char **argv){
    if(argc==2&&strcmp(argv[1],"--font-digests")==0){lv_init();font_bitmap_digests(true);lv_deinit();return 0;}
    bool tiles=argc==2 && !strcmp(argv[1],"--tile-contract");
    const char *folder=argc>1&&!tiles?argv[1]:NULL;
    device_format=getenv("SOROUND_TEST_DEVICE_FORMAT")!=NULL;
    setenv("TZ","UTC",1);tzset();struct tm t={.tm_year=126,.tm_mon=9,.tm_mday=5,.tm_hour=10,.tm_min=8,.tm_sec=21};now=mktime(&t);
    ip_ready=false;snapshot(true);assert(!s_data.wifi && !s_data.ip[0]);ip_ready=true;snapshot(true);assert(s_data.wifi);
    for(int i=0;i<3;++i){images[i]=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=466,.h=466,.stride=932},.data_size=sizeof image_pixels[i],.data=(uint8_t *)image_pixels[i]};
        if(argc>2){char path[1024];snprintf(path,sizeof path,"%s/%s.rgb565",argv[2],watchface_theme_name(i));FILE *f=fopen(path,"rb");assert(f);assert(fread(image_pixels[i],1,sizeof image_pixels[i],f)==sizeof image_pixels[i]);assert(fclose(f)==0);}}
    lv_init();font_bitmap_digests(false);i18n_init();display=lv_display_create(466,466);
    // 同时覆盖真机40行双缓冲与交换字节后端；输出统一回RGB565以直接比较像素。
    // Exercise the device's 40-row double buffers and swapped backend; normalize captured pixels for exact comparisons.
    lv_display_set_color_format(display,device_format?LV_COLOR_FORMAT_RGB565_SWAPPED:LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,buffer,device_format?second_buffer:NULL,device_format?466*40*2:sizeof buffer,LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
    lv_obj_set_style_bg_color(lv_screen_active(),lv_color_black(),0);
    catalogue_contract();studio_font_contract();typography_font_contract();watchface_init();assert(watchface_selected()==4&&!watchface_visible());watchface_show();assert(wf_timer);
    if(tiles){studio_tile_contract();lv_deinit();return 0;}
    uint64_t hashes[WATCHFACE_COUNT];
    for(int i=0;i<WATCHFACE_COUNT;++i){watchface_select(i);char name[32];snprintf(name,sizeof name,"face-%02d",i);hashes[i]=capture(folder,name);for(int j=0;j<i;++j)assert(hashes[i]!=hashes[j]);assert(lv_obj_get_child_count(wf_content)==0);}
    capture_studio_references(folder);sport_time_contract();
    // HAND 与 STUDIO 不得按旧五款分类误读天气或图片。
    // HAND and STUDIO must not inherit legacy weather/image data sources.
    int polls=weather_polls,reads=image_reads;
    for(int i=15;i<WATCHFACE_COUNT;++i){
        watchface_select(i);uint64_t before=capture(NULL,"seconds-before");flushed=0;++now;tick(NULL);
        uint64_t after=capture(NULL,"seconds-after");
        // FOLD/HORIZON与极简排版只依赖分钟；其他HAND/STUDIO须真实体现秒变化。
        // FOLD/HORIZON and typography depend on minutes; other HAND/STUDIO faces visibly encode seconds.
        bool minute_only=i==25||i==27||(i>=44&&i<50);
        bool expected=minute_only?(before==after && flushed==0):(before!=after && flushed>0);
        if(!expected)fprintf(stderr,"second update failed: ID %d %s minute_only=%d before=%016llx after=%016llx flushed=%u\n",
            i,watchface_name(i),minute_only,(unsigned long long)before,(unsigned long long)after,flushed);
        assert(expected);
        if(minute_only){
            now+=60;tick(NULL);after=capture(NULL,"minute-only-after");expected=before!=after && flushed>0;
            if(!expected)fprintf(stderr,"minute update failed: ID %d %s before=%016llx after=%016llx flushed=%u\n",
                i,watchface_name(i),(unsigned long long)before,(unsigned long long)after,flushed);
            assert(expected);now-=60;
        }
        --now;
    }
    assert(weather_polls==polls&&image_reads==reads);
    // At 10:08, minute progress must leave six o'clock dark; the hour ring must have passed it.
    watchface_select(12);capture(NULL,"ring-semantics");
    // Between the 30/31 minute dots, the unused outer track must be black.
    assert(((pixels[442*466+244]>>5)&63)<=13);
    assert(((pixels[398*466+233]>>5)&63)>15);
    // The unused portion is dotted, not a duplicate complete solid ring.
    assert(pixels[85*466+153]==0);
    for(int i=0;i<WATCHFACE_COUNT;++i){
        watchface_select(i);watchface_set_aod(true);char name[32];snprintf(name,sizeof name,"aod-%02d",i);
        uint64_t hash=capture(folder,name);assert(wf_timer->period==39000);
        now+=9;flushed=0;tick(NULL);uint64_t after=capture(NULL,"same-minute");
        bool stable=hash==after && flushed==0;
        if(!stable)fprintf(stderr,"AOD seconds changed: ID %d %s before=%016llx after=%016llx flushed=%u\n",
            i,watchface_name(i),(unsigned long long)hash,(unsigned long long)after,flushed);
        assert(stable);now+=60;tick(NULL);after=capture(NULL,"next-minute");
        bool changed=hash!=after && flushed>0;
        if(!changed)fprintf(stderr,"AOD minute unchanged: ID %d %s before=%016llx after=%016llx flushed=%u\n",
            i,watchface_name(i),(unsigned long long)hash,(unsigned long long)after,flushed);
        assert(changed);
        now-=69;watchface_set_aod(false);assert(wf_timer->period==1000);
    }
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
    watchface_select(-1);assert(watchface_selected()==0);watchface_select(100);assert(watchface_selected()==55);
    watchface_hide();assert(!wf_timer&&!watchface_visible());watchface_select(9);watchface_show();assert(watchface_selected()==9);
    for(int i=21;i<WATCHFACE_COUNT;++i){
        watchface_select(i);capture(NULL,"studio-before-sleep");polls=weather_polls;reads=image_reads;
        watchface_set_sleep(true);assert(!wf_timer && watchface_visible());time_t before=now;now+=60;
        watchface_set_sleep(false);assert(wf_timer && wf_timer->period==1000);
        assert(s_data.time.tm_min==localtime(&now)->tm_min);capture(NULL,"studio-after-wake");
        assert(weather_polls==polls && image_reads==reads);now=before;
        watchface_hide();assert(!wf_timer && !watchface_visible());watchface_show();assert(wf_timer);
    }
    watchface_hide();lv_deinit();puts("56 distinct native faces with preserved 44 IDs, HAND/STUDIO seconds, FOLD/HORIZON/typography minute-only updates, catalogue boundaries, AOD seconds/minutes, sleep/wake, selection, weather ranges/failure, 32-byte SSID, unavailable battery, image loading/custom priority passed.");return 0;
}
