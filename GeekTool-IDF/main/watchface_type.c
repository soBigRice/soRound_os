#include "watchface_type.h"
#include "watchface_type_fonts.h"
#include "watchface_studio_draw.h"

static const uint32_t cream=0xeeeae0, dim=0x7d7e79;

static void number(const wfs_draw_t *d, const char *text, float x, float y,
                   int size, wft_weight_t weight, uint32_t color, float spacing)
{
    const lv_font_t *font=wft_font(size,d->preview,weight);
    if(font)wfs_label_font(d,text,x,y,font,color,spacing);
}

static void parts(const watchface_data_t *data, char hour[3], char minute[3])
{
    snprintf(hour,3,"%02u",(unsigned)data->time.tm_hour%24u);
    snprintf(minute,3,"%02u",(unsigned)data->time.tm_min%60u);
}

static void caption(const wfs_draw_t *d, const watchface_data_t *data,
                    float x, float y, int size, float spacing)
{
    char text[32];wfs_date(data,text,sizeof text);wfs_text(d,text,x,y,size,dim,spacing);
}

void watchface_type_render(lv_layer_t *layer, const lv_area_t *area,
                           const watchface_data_t *data, int kind, bool preview)
{
    static const char *const names[]={"STACK","AIR","OFFSET","COLUMN","CUT","EDITION"};
    if(!data || !area || kind<0 || kind>=6)return;
    wfs_draw_t d=wfs_make(layer,area,preview);
    if(data->aod){wfs_aod(&d,data,names[kind]);return;}
    char hour[3],minute[3];parts(data,hour,minute);
    switch(kind){
    case 0:
        caption(&d,data,233,71,12,3);
        number(&d,hour,233,224,202,WFT_BLACK,cream,0);
        number(&d,minute,233,389,202,WFT_BLACK,cream,0);
        wfs_bar(&d,142,243,324,243,3,0xe15e46);
        break;
    case 1:{
        static const char *const days[]={"SUN","MON","TUE","WED","THU","FRI","SAT"};
        wfs_text(&d,days[LV_CLAMP(0,data->time.tm_wday,6)],233,131,12,dim,5);
        char text[8];wfs_time(data,text,sizeof text);
        number(&d,text,233,284,161,WFT_REGULAR,cream,-6);
        snprintf(text,sizeof text,"%02u / %02u",(unsigned)data->time.tm_mday%32u,(unsigned)(data->time.tm_mon+1)%13u);
        wfs_text(&d,text,233,342,14,dim,5);
        wfs_bar(&d,216,379,250,379,1,0xb9b5a8);
        break;}
    case 2:
        number(&d,hour,189,225,215,WFT_MEDIUM,cream,0);
        number(&d,minute,289,379,176,WFT_REGULAR,0xb5b4ad,-3);
        caption(&d,data,121,322,11,2);
        wfs_disc(&d,354,157,6,0xe6a76b);
        break;
    case 3:
        wfs_text(&d,"H",156,91,12,dim,3);wfs_text(&d,"M",307,91,12,dim,3);
        wfs_bar(&d,233,107,233,373,1,0x30322f);
        for(int digit=0;digit<2;digit++){
            char h[]={hour[digit],0},m[]={minute[digit],0};
            number(&d,h,157,234+digit*139,166,WFT_MEDIUM,cream,0);
            number(&d,m,308,234+digit*139,166,WFT_REGULAR,0xa5b9ac,0);
        }
        caption(&d,data,233,424,11,3);
        break;
    case 4:
        number(&d,hour,233,223,220,WFT_BLACK,cream,0);
        number(&d,minute,233,393,220,WFT_BLACK,cream,0);
        // 负空间切口在字形之后绘制，保留每个数字的轮廓与读时顺序。
        // Draw negative-space cuts after glyphs while preserving each numeral's silhouette and reading order.
        wfs_rect(&d,103,168,261,4,0,0x000000);wfs_rect(&d,103,338,261,4,0,0x000000);
        char day[3];snprintf(day,sizeof day,"%02u",(unsigned)data->time.tm_mday%32u);
        wfs_text(&d,day,233,433,13,0xe15e46,2);
        break;
    case 5:
        wfs_text(&d,"T I M E",233,104,12,dim,2);
        number(&d,hour,176,300,200,WFT_BLACK,cream,0);
        wfs_text(&d,minute,350,282,88,0xd0c9b7,-2);
        wfs_bar(&d,87,325,379,325,1,0xc9c4b7);
        caption(&d,data,233,361,12,3);
        break;
    }
}
