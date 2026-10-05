#include "watchface_ui.h"
#include "app.h"
#include "weather_artwork.h"
#include "tools_ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define WHITE 0xf3f1eb
#define GRAY  0x9a9aa0
#define DIM   0x29292e
#define PI 3.14159265358979323846f

typedef struct {
    lv_layer_t *layer;
    int x, y, size;
    bool preview;
} draw_t;
static int scaled(const draw_t *d, float v) {return (int)lroundf(v*d->size/466.0f);}
static void dot(const draw_t *d,float x,float y,float diameter,uint32_t color) {
    tools_dot(d->layer,d->x+scaled(d,x),d->y+scaled(d,y),LV_MAX(1,scaled(d,diameter)),color);
}
static void line(const draw_t *d,float x1,float y1,float x2,float y2,float width,uint32_t color) {
    tools_line(d->layer,d->x+scaled(d,x1),d->y+scaled(d,y1),d->x+scaled(d,x2),d->y+scaled(d,y2),LV_MAX(1,scaled(d,width)),color);
}
static void bar(const draw_t *d,float x1,float y1,float x2,float y2,float width,uint32_t color) {
    lv_draw_line_dsc_t l;lv_draw_line_dsc_init(&l);
    l.p1=(lv_point_precise_t){d->x+scaled(d,x1),d->y+scaled(d,y1)};
    l.p2=(lv_point_precise_t){d->x+scaled(d,x2),d->y+scaled(d,y2)};
    l.width=LV_MAX(1,scaled(d,width));l.color=lv_color_hex(color);
    // Rectangular clock ticks must not inherit the tool UI's round line caps.
    lv_draw_line(d->layer,&l);
}
static void arc(const draw_t *d,int radius,float start,float end,int width,uint32_t color) {
    if(end<=start)return;
    lv_draw_arc_dsc_t a;lv_draw_arc_dsc_init(&a);
    a.center=(lv_point_t){d->x+scaled(d,233),d->y+scaled(d,233)};
    a.radius=scaled(d,radius);a.width=LV_MAX(1,scaled(d,width));a.color=lv_color_hex(color);
    // LVGL's angle fields are unsigned. Normalize negative clock angles before assignment.
    bool full=end-start>=360.0f;
    a.start_angle=full?0:fmodf(start+360.0f,360.0f);
    a.end_angle=full?360:fmodf(end+360.0f,360.0f);
    // Thick hour segments need flat ends; round caps would close the four-degree gaps.
    a.rounded=width<=7;lv_draw_arc(d->layer,&a);
}
static const lv_font_t *font(const draw_t *d,int size) {
    switch(size) {
        case 14:return d->preview?&font_wf_7:&font_wf_14;
        case 18:return d->preview?&font_wf_9:&font_wf_18;
        case 28:return d->preview?&font_wf_semibold_14:&font_wf_semibold_28;
        case 52:return d->preview?&font_wf_semibold_26:&font_wf_semibold_52;
        case 56:return d->preview?&font_wf_semibold_28:&font_wf_semibold_56;
        case 72:return d->preview?&font_wf_36:&font_wf_72;
        case 96:return d->preview?&font_wf_regular_48:&font_wf_regular_96;
        case 104:return d->preview?&font_wf_semibold_52:&font_wf_semibold_104;
        case 112:return d->preview?&font_wf_semibold_56:&font_wf_semibold_112;
        case 188:return d->preview?&font_wf_black_94:&font_wf_black_188;
        default:return d->preview?&font_wf_82:&font_wf_164;
    }
}
static void aligned_text(const draw_t *d,const char *value,int x,int cy,int width,int size,uint32_t color,int spacing,bool left) {
    if(!value[0])return;
    const lv_font_t *f=font(d,size);lv_point_t bounds;
    int w=scaled(d,width),space=scaled(d,spacing);
    lv_text_get_size(&bounds,value,f,space,0,w,LV_TEXT_FLAG_NONE);
    lv_draw_label_dsc_t l;lv_draw_label_dsc_init(&l);
    l.font=f;l.text=value;l.text_local=1;l.color=lv_color_hex(color);l.align=left?LV_TEXT_ALIGN_LEFT:LV_TEXT_ALIGN_CENTER;l.letter_space=space;
    int start=d->x+scaled(d,x)-(left?0:w/2),top=d->y+scaled(d,cy)-bounds.y/2;
    lv_area_t a={start,top,start+w-1,top+bounds.y-1};lv_draw_label(d->layer,&l,&a);
}
static void text(const draw_t *d,const char *value,int cx,int cy,int width,int size,uint32_t color,int spacing) {
    aligned_text(d,value,cx,cy,width,size,color,spacing,false);
}
static void date_at(const draw_t *d,const struct tm *t,int x,int cy,bool left) {
    static const char *const days[]={"SUN","MON","TUE","WED","THU","FRI","SAT"};
    static const char *const months[]={"JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"};
    char value[24];snprintf(value,sizeof value,"%s %02d %s",days[t->tm_wday],t->tm_mday,months[t->tm_mon]);
    aligned_text(d,value,x,cy,290,18,GRAY,2,left);
}
static void date(const draw_t *d,const struct tm *t,int cy) {date_at(d,t,233,cy,false);}
static void clock_text(const draw_t *d,const char *value,int cx,int cy,int size,bool red_colon) {
    text(d,value,cx,cy,374,size,WHITE,0);
    if(!red_colon)return;
    // Position the colored colon with the same proportional advances as the full label.
    const lv_font_t *f=font(d,size);lv_point_t total,prefix,colon;
    char hours[3]={value[0],value[1],0};
    lv_text_get_size(&total,value,f,0,0,scaled(d,374),LV_TEXT_FLAG_NONE);
    lv_text_get_size(&prefix,hours,f,0,0,scaled(d,374),LV_TEXT_FLAG_NONE);
    lv_text_get_size(&colon,":",f,0,0,scaled(d,374),LV_TEXT_FLAG_NONE);
    int x=d->x+scaled(d,cx)-total.x/2+prefix.x;
    lv_draw_label_dsc_t l;lv_draw_label_dsc_init(&l);l.font=f;l.text=":";l.color=lv_color_hex(COL_RED);
    lv_area_t a={x,d->y+scaled(d,cy)-total.y/2,x+colon.x-1,d->y+scaled(d,cy)-total.y/2+total.y-1};
    lv_draw_label(d->layer,&l,&a);
}
// Local numeral shapes remove the old slashed zero without changing shared timer glyphs.
static const char *const digits[10][7]={
    {"01110","10001","10001","10001","10001","10001","01110"},
    {"00100","01100","00100","00100","00100","00100","01110"},
    {"01110","10001","00001","00110","01000","10000","11111"},
    {"11110","00001","00001","01110","00001","00001","11110"},
    {"00010","00110","01010","10010","11111","00010","00010"},
    {"11111","10000","10000","11110","00001","00001","11110"},
    {"01110","10000","10000","11110","10001","10001","01110"},
    {"11111","00001","00010","00100","01000","01000","01000"},
    {"01110","10001","10001","01110","10001","10001","01110"},
    {"01110","10001","10001","01111","00001","00001","01110"}
};
static void matrix(const draw_t *d,const char *value,int cx,int cy,int pitch,int diameter) {
    int count=(int)strlen(value),columns=count==5?25:count*6-1;
    float x=cx-(columns-1)*pitch/2.0f,y=cy-3*pitch;
    for(int i=0;i<count;++i) {
        if(value[i]==':') {dot(d,x,y+2*pitch,diameter,COL_RED);dot(d,x,y+4*pitch,diameter,COL_RED);x+=2*pitch;continue;}
        if(value[i]>='0'&&value[i]<='9')for(int row=0;row<7;++row)for(int col=0;col<5;++col)
            if(digits[value[i]-'0'][row][col]=='1')dot(d,x+col*pitch,y+row*pitch,diameter,WHITE);
        x+=6*pitch;
    }
}
static void ticks(const draw_t *d,int radius,int active,bool bars) {
    for(int i=0;i<60;++i) {
        float a=i*PI/30-PI/2;bool major=i%5==0;
        uint32_t c=i==active?COL_RED:major?WHITE:GRAY;
        if(bars) {
            int inner=radius-(major?14:8);
            bar(d,233+cosf(a)*inner,233+sinf(a)*inner,233+cosf(a)*radius,233+sinf(a)*radius,major?3:2,c);
        } else dot(d,233+cosf(a)*radius,233+sinf(a)*radius,i==active?7:major?6:3,c);
    }
}
static void wifi_icon(const draw_t *d,int cx,int cy,uint32_t color) {
    for(int i=0;i<3;++i) {
        int radius=7+i*5;
        for(int j=0;j<9;++j) {float a=(-138+j*12)*PI/180;dot(d,cx+cosf(a)*radius,cy+sinf(a)*radius,2,color);}
    }
    dot(d,cx,cy,3,color);
}
static void battery_icon(const draw_t *d,int cx,int cy,const watchface_data_t *data) {
    uint32_t color=data->charging?COL_CHARGE:(data->battery_valid&&data->battery<=15?COL_RED:GRAY);
    line(d,cx-11,cy-5,cx+8,cy-5,1,color);line(d,cx-11,cy+5,cx+8,cy+5,1,color);
    line(d,cx-11,cy-5,cx-11,cy+5,1,color);line(d,cx+8,cy-5,cx+8,cy+5,1,color);
    line(d,cx+11,cy-2,cx+11,cy+2,2,color);
    if(data->battery_valid&&data->battery>0)line(d,cx-8,cy,cx-8+LV_MAX(1,14*data->battery/100),cy,5,color);
}
static void status(const draw_t *d,const watchface_data_t *data,int theme) {
    char battery[16];if(data->battery_valid)snprintf(battery,sizeof battery,"%d%%%s",data->battery,data->charging?" +":"");else strcpy(battery,"--%");
    uint32_t c=data->battery_valid&&data->battery<=15?COL_RED:GRAY;
    const char *ssid=data->wifi?data->ssid:"Wi-Fi off";
    lv_point_t ssid_size;lv_text_get_size(&ssid_size,ssid,font(d,14),0,0,scaled(d,466),LV_TEXT_FLAG_NONE);
    // Keep the reference's compact row for normal names; long names get an
    // explicit wide row rather than covering IP/battery or leaving the circle.
    if(theme==0) {
        if(ssid_size.x>scaled(d,104)) {
            wifi_icon(d,233,290,data->wifi?WHITE:DIM);text(d,ssid,233,318,300,14,GRAY,0);
            text(d,data->ip,200,354,188,14,GRAY,0);battery_icon(d,304,354,data);text(d,battery,351,354,68,14,c,0);
        } else {
            wifi_icon(d,125,331,data->wifi?WHITE:DIM);text(d,ssid,125,355,104,14,WHITE,0);
            line(d,174,327,174,358,1,GRAY);text(d,data->ip,241,343,125,14,GRAY,0);
            line(d,313,327,313,358,1,GRAY);battery_icon(d,354,331,data);text(d,battery,354,355,68,14,c,0);
        }
    } else if(theme==1) {
        if(ssid_size.x>scaled(d,140)) {
            wifi_icon(d,233,339,data->wifi?WHITE:DIM);text(d,ssid,233,363,280,14,GRAY,0);
        } else {wifi_icon(d,176,359,data->wifi?WHITE:DIM);text(d,ssid,252,359,140,18,GRAY,1);}
        text(d,data->ip,233,388,240,18,GRAY,1);battery_icon(d,207,416,data);text(d,battery,257,416,75,18,c,0);
    } else {
        wifi_icon(d,98,344,data->wifi?WHITE:DIM);
        bool long_name=ssid_size.x>scaled(d,180);
        aligned_text(d,ssid,131,340,long_name?260:220,long_name?14:18,WHITE,long_name?0:1,true);
        aligned_text(d,data->ip,131,375,235,18,WHITE,1,true);
        battery_icon(d,150,409,data);aligned_text(d,battery,179,409,88,18,c,0,true);
    }
}
static void weather_icon(const draw_t *d,int code,int cx,int cy,int width) {
    const weather_artwork_t *art=weather_artwork_for(code,true);if(!art)return;
    lv_draw_image_dsc_t image;lv_draw_image_dsc_init(&image);image.src=art->image;image.pivot=(lv_point_t){0,0};
    image.scale_x=image.scale_y=scaled(d,width)*256/(int)art->image->header.w;
    image.antialias=1;
    int height=(int)art->image->header.h*image.scale_y/256;
    int left=d->x+scaled(d,cx)-scaled(d,width)/2,top=d->y+scaled(d,cy)-height/2;
    lv_area_t a={left,top,left+(int)art->image->header.w-1,top+(int)art->image->header.h-1};
    lv_draw_image(d->layer,&image,&a);
}
static void rings(const draw_t *d,const watchface_data_t *data,int theme,const char *time) {
    int minute=data->time.tm_min,hour=data->time.tm_hour%12;
    if(theme==0) {
        ticks(d,211,minute,true);
        for(int i=0;i<60;++i){float a=i*PI/30-PI/2;dot(d,233+cosf(a)*152,233+sinf(a)*152,i%5==0?12:2,i/5==hour&&i%5==0?COL_RED:i%5==0?WHITE:GRAY);}
        clock_text(d,time,233,229,96,false);date(d,&data->time,290);
    } else if(theme==1) {
        for(int i=0;i<60;++i) {
            float a=i*PI/30-PI/2;uint32_t c=i<minute?WHITE:DIM;
            bar(d,233+cosf(a)*194,233+sinf(a)*194,233+cosf(a)*214,233+sinf(a)*214,7,c);
        }
        for(int i=0;i<12;++i)arc(d,149,i*30-90+1,i*30-90+28,21,i<hour?WHITE:DIM);
        float a=minute*PI/30-PI/2;dot(d,233+cosf(a)*204,233+sinf(a)*204,17,COL_RED);
        a=hour*PI/6-PI/2;dot(d,233+cosf(a)*139,233+sinf(a)*139,27,0x000000);dot(d,233+cosf(a)*139,233+sinf(a)*139,21,COL_RED);
        clock_text(d,time,233,229,96,true);date(d,&data->time,286);
    } else {
        for(int i=0;i<60;++i){float a=i*PI/30-PI/2;
            if(i>minute)dot(d,233+cosf(a)*211,233+sinf(a)*211,4,GRAY);
            if(i>hour*5)dot(d,233+cosf(a)*168,233+sinf(a)*168,3,GRAY);
        }
        arc(d,211,-90,-90+minute*6,7,GRAY);arc(d,168,-90,-90+hour*30,7,0x55555b);
        float a=minute*PI/30-PI/2;dot(d,233+cosf(a)*208,233+sinf(a)*208,18,COL_RED);
        a=hour*PI/6-PI/2;dot(d,233+cosf(a)*165,233+sinf(a)*165,14,WHITE);
        date(d,&data->time,164);clock_text(d,time,233,241,104,false);
        line(d,153,331,170,331,5,WHITE);text(d,"HOUR",202,331,58,14,GRAY,1);
        line(d,249,331,266,331,5,0x55555b);text(d,"MIN",296,331,44,14,GRAY,1);
    }
}
static void broken_orbit(const draw_t *d) {
    // Six separate strokes leave the gaps visible in the approved ORBIT board.
    static const int spans[][2]={{-76,-29},{-24,24},{29,76},{104,151},{156,204},{209,256}};
    for(unsigned i=0;i<sizeof spans/sizeof spans[0];++i)arc(d,208,spans[i][0],spans[i][1],3,0x6c6c72);
}
static void temperature_range(const draw_t *d,const watchface_data_t *data,int y,bool arrows) {
    if(!data->weather_valid)return;
    char low[16],high[16];snprintf(low,sizeof low,"%d°",data->low);snprintf(high,sizeof high,"%d°",data->high);
    text(d,low,arrows?173:185,y,94,28,WHITE,0);text(d,high,arrows?320:282,y,94,28,WHITE,0);
    line(d,233,y-12,233,y+12,2,arrows?GRAY:COL_RED);
    if(arrows)for(int i=0;i<2;++i) {
        int x=i?273:126,tip=y+(i?-9:9);
        line(d,x,y-9,x,y+9,3,GRAY);line(d,x-7,tip+(i?7:-7),x,tip,3,GRAY);line(d,x,tip,x+7,tip+(i?7:-7),3,GRAY);
    }
}
static void weather(const draw_t *d,const watchface_data_t *data,int theme,const char *time) {
    char temperature[16],humidity[24];
    if(data->weather_valid) {
        snprintf(temperature,sizeof temperature,"%d°",data->temperature);
        snprintf(humidity,sizeof humidity,"HUM %d%%",data->humidity);
    } else {strcpy(temperature,"--°");strcpy(humidity,"Weather unavailable");}
    if(theme==0) {
        if(data->weather_valid)weather_icon(d,data->code,169,113,128);
        text(d,temperature,306,113,146,56,WHITE,0);text(d,humidity,304,154,210,18,GRAY,1);
        clock_text(d,time,233,239,112,false);temperature_range(d,data,337,true);date(d,&data->time,389);
    } else {
        if(theme==1)broken_orbit(d);
        date(d,&data->time,theme==1?100:205);clock_text(d,time,233,theme==1?174:135,112,false);
        if(theme==2)line(d,213,234,253,234,4,COL_RED);
        if(data->weather_valid)weather_icon(d,data->code,169,290,120);
        text(d,temperature,305,285,146,56,WHITE,0);text(d,humidity,305,331,212,18,GRAY,1);
        temperature_range(d,data,383,theme==1);
    }
}
static void image_face(const draw_t *d,const watchface_data_t *data,int theme,const char *time) {
    if(data->image&&!data->aod) {
        lv_draw_image_dsc_t image;lv_draw_image_dsc_init(&image);image.src=data->image;
        image.pivot=(lv_point_t){0,0};image.scale_x=image.scale_y=d->size*256/466;
        image.clip_radius=LV_RADIUS_CIRCLE;image.antialias=1;
        // Tone mapping protects white time against arbitrary custom backgrounds without a black card.
        image.recolor=lv_color_black();image.recolor_opa=LV_OPA_20;
        // A bright custom image must still support readable white time and gray date.
        // Sample only the time/date region; dark default photographs retain their normal detail.
        if(data->image->header.cf==LV_COLOR_FORMAT_RGB565&&data->image->header.w==466&&data->image->header.h==466) {
            const uint16_t *pixels=(const uint16_t *)data->image->data;
            int stride=data->image->header.stride/2;
            if(stride>=466)for(int y=80;y<218;y+=16)for(int x=120;x<346;x+=16) {
                uint16_t p=pixels[y*stride+x];
                int luminance=(((p>>11)&31)*255/31*299+((p>>5)&63)*255/63*587+(p&31)*255/31*114)/1000;
                if(luminance>140)image.recolor_opa=LV_OPA_70;
            }
        }
        lv_area_t a={d->x,d->y,d->x+465,d->y+465};lv_draw_image(d->layer,&image,&a);
    }
    if(theme==0) {date(d,&data->time,104);text(d,time,233,179,354,104,WHITE,0);}
    else if(theme==1) {text(d,time,233,137,334,104,WHITE,0);date(d,&data->time,204);}
    else {line(d,84,81,116,81,4,COL_RED);aligned_text(d,time,75,143,350,104,WHITE,0,true);date_at(d,&data->time,75,201,true);}
    if(!data->image&&!data->aod)text(d,data->image_loading?"Loading background":"Background unavailable",233,316,300,18,GRAY,0);
}
void watchface_render(lv_layer_t *layer,const lv_area_t *area,const watchface_data_t *data,int index,bool preview) {
    draw_t d={.layer=layer,.x=area->x1,.y=area->y1,.size=lv_area_get_width(area),.preview=preview};
    int theme=index/WATCHFACE_KIND_COUNT,kind=index%WATCHFACE_KIND_COUNT;
    char time[8],hours[4],minutes[4];snprintf(time,sizeof time,"%02d:%02d",data->time.tm_hour,data->time.tm_min);
    snprintf(hours,sizeof hours,"%02d",data->time.tm_hour);snprintf(minutes,sizeof minutes,"%02d",data->time.tm_min);
    if(kind==4)image_face(&d,data,theme,time);
    else if(kind==3)weather(&d,data,theme,time);
    else if(kind==2)rings(&d,data,theme,time);
    else if(kind==1) {
        if(theme==0){text(&d,hours,233,138,285,188,WHITE,0);text(&d,minutes,233,310,285,188,WHITE,0);line(&d,212,223,254,223,4,COL_RED);date(&d,&data->time,402);}
        else if(theme==1){broken_orbit(&d);date(&d,&data->time,120);clock_text(&d,time,233,233,112,true);dot(&d,233,353,11,COL_RED);}
        else {text(&d,hours,174,135,245,188,WHITE,0);text(&d,minutes,296,304,245,188,WHITE,0);line(&d,207,227,259,227,6,COL_RED);char day[8],date_value[16];strftime(day,sizeof day,"%a",&data->time);strftime(date_value,sizeof date_value,"%d %b",&data->time);for(unsigned i=0;day[i];++i)day[i]=(char)toupper((unsigned char)day[i]);for(unsigned i=0;date_value[i];++i)date_value[i]=(char)toupper((unsigned char)date_value[i]);text(&d,day,136,310,100,18,GRAY,2);text(&d,date_value,136,340,132,18,GRAY,1);}
    } else {
        if(theme==0){ticks(&d,211,data->aod?-1:data->time.tm_sec,false);date(&d,&data->time,126);matrix(&d,time,233,222,14,10);}
        else if(theme==1){ticks(&d,211,data->aod?-1:data->time.tm_sec,false);date(&d,&data->time,91);matrix(&d,hours,233,164,13,10);matrix(&d,minutes,233,284,13,10);dot(&d,233,222,10,COL_RED);}
        else {matrix(&d,time,218,183,14,11);date_at(&d,&data->time,60,263,true);for(int i=0;i<5;++i)dot(&d,427,188+i*24,i==2?11:6,i==2?COL_RED:GRAY);}
        status(&d,data,theme);
    }
    if(theme!=2&&kind!=2&&!(theme==1&&kind==4))dot(&d,233,40,theme==1?11:7,COL_RED);
}
