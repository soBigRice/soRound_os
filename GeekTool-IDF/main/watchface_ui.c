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
        case 24:return d->preview?&font_hand_regular_12:&font_hand_regular_24;
        case 32:return d->preview?&font_hand_semibold_16:&font_hand_semibold_32;
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
static void needle_polygon(const draw_t *d,float angle,const float points[][2],int count,uint32_t color) {
    float p[24][2],s=sinf(angle),c=cosf(angle),scale=d->size/466.0f;
    float top=INFINITY,bottom=-INFINITY;
    for(int i=0;i<count;++i){p[i][0]=d->x+(233+points[i][0]*c-points[i][1]*s)*scale;
        p[i][1]=d->y+(233+points[i][0]*s+points[i][1]*c)*scale;
        top=fminf(top,p[i][1]);bottom=fmaxf(bottom,p[i][1]);}
    // Triangle fans antialias their shared edges, leaving seams inside solid hands.
    // Fill each convex contour once; only its outside edge gets four-sample coverage.
    lv_draw_rect_dsc_t r;lv_draw_rect_dsc_init(&r);r.bg_color=lv_color_hex(color);
    for(int y=(int)floorf(top);y<=(int)ceilf(bottom);++y){
        float left[4],right[4],lo=INFINITY,hi=-INFINITY,solid_lo=-INFINITY,solid_hi=INFINITY;
        int samples=0;
        for(int sample=0;sample<4;++sample){float sy=y-.375f+sample*.25f;left[sample]=INFINITY;right[sample]=-INFINITY;
            for(int i=0,j=count-1;i<count;j=i++)if((p[i][1]>sy)!=(p[j][1]>sy)){
                float x=p[i][0]+(sy-p[i][1])*(p[j][0]-p[i][0])/(p[j][1]-p[i][1]);
                left[sample]=fminf(left[sample],x);right[sample]=fmaxf(right[sample],x);}
            if(left[sample]<=right[sample]){++samples;lo=fminf(lo,left[sample]);hi=fmaxf(hi,right[sample]);
                solid_lo=fmaxf(solid_lo,left[sample]);solid_hi=fminf(solid_hi,right[sample]);}
        }
        if(!samples)continue;
        int first=(int)floorf(lo+.5f),last=(int)ceilf(hi-.5f);
        int a=samples==4?(int)ceilf(solid_lo+.5f):last+1,b=samples==4?(int)floorf(solid_hi-.5f):first-1;
        if(a<=b){r.bg_opa=LV_OPA_COVER;lv_area_t area={a,y,b,y};lv_draw_rect(d->layer,&r,&area);}
        for(int x=first;x<=last;++x){if(x>=a&&x<=b)continue;float coverage=0;
            for(int sample=0;sample<4;++sample)if(left[sample]<=right[sample]){
                coverage+=fminf(1,fmaxf(0,right[sample]-x+.5f))-fminf(1,fmaxf(0,left[sample]-x+.5f));}
            r.bg_opa=(lv_opa_t)lroundf(coverage*255/4);
            if(r.bg_opa){lv_area_t area={x,y,x,y};lv_draw_rect(d->layer,&r,&area);}
        }
    }
}
static void needle(const draw_t *d,float angle,float length,float width,uint32_t color,bool hollow,bool flat) {
    const float p[][2]={{-width/2,14},{-width/2,-length+(flat?0:12)},{0,-length},{width/2,-length+(flat?0:12)},{width/2,14}};
    needle_polygon(d,angle,p,5,color);
    if(hollow)bar(d,233+sinf(angle)*22,233-cosf(angle)*22,233+sinf(angle)*(length-23),233-cosf(angle)*(length-23),width-7,0);
}
static void leaf(const draw_t *d,float angle,float length,float width,uint32_t color) {
    float p[20][2];int n=0;
    for(int i=0;i<=9;++i){float t=i/9.0f;p[n][0]=-width*.5f*sinf(PI*t);p[n++][1]=14-t*(length+14);}
    for(int i=8;i>0;--i){float t=i/9.0f;p[n][0]=width*.5f*sinf(PI*t);p[n++][1]=14-t*(length+14);}
    needle_polygon(d,angle,p,n,color);
}
static void hand_date(const draw_t *d,const struct tm *t,int y,int size) {
    char value[24];strftime(value,sizeof value,"%a %d %b",t);
    for(char *p=value;*p;++p)*p=(char)toupper((unsigned char)*p);
    text(d,value,233,y,230,size,GRAY,0);
}
static void hand_battery(const draw_t *d,const watchface_data_t *data,int y) {
    uint32_t color=data->charging?COL_CHARGE:GRAY;char value[12];
    bar(d,203,y-4,219,y-4,1,color);bar(d,203,y+4,219,y+4,1,color);
    bar(d,203,y-4,203,y+4,1,color);bar(d,219,y-4,219,y+4,1,color);bar(d,222,y-2,222,y+2,2,color);
    if(data->battery_valid){bar(d,206,y,206+data->battery*.12f,y,4,color);snprintf(value,sizeof value,"%d%%",data->battery);}
    else strcpy(value,"--%");
    text(d,value,249,y,58,14,color,0);
}
static void hand_face(const draw_t *d,const watchface_data_t *data,int kind) {
    bool aod=data->aod;uint32_t shade=aod?0x8d8d87:WHITE;
    float seconds=aod?0:data->time.tm_sec,minutes=data->time.tm_min+seconds/60;
    float hours=data->time.tm_hour%12+minutes/60,ha=hours*PI/6,ma=minutes*PI/30,sa=seconds*PI/30;
    if(kind==0){
        for(int i=0;i<60;++i){bool major=i%5==0;float a=i*PI/30;if(aod&&!major)continue;
            bar(d,233+sinf(a)*(major?190:203),233-cosf(a)*(major?190:203),233+sinf(a)*214,233-cosf(a)*214,major?4:1.5f,major?shade:0x73737b);}
        text(d,"12",233,83,64,32,shade,0);text(d,"3",382,235,48,32,shade,0);text(d,"6",233,383,48,32,shade,0);text(d,"9",84,235,48,32,shade,0);
        if(!aod){text(d,"soRound",233,150,120,14,GRAY,0);hand_date(d,&data->time,308,18);hand_battery(d,data,337);}
        needle(d,ha,107,14,shade,false,false);needle(d,ma,158,8,shade,false,false);
        if(!aod){line(d,233-sinf(sa)*31,233+cosf(sa)*31,233+sinf(sa)*179,233-cosf(sa)*179,2,COL_RED);
            tools_circle(d->layer,d->x+scaled(d,233-sinf(sa)*23),d->y+scaled(d,233+cosf(sa)*23),scaled(d,6),LV_MAX(1,scaled(d,2)),COL_RED);}
        dot(d,233,233,18,0);dot(d,233,233,11,aod?shade:COL_RED);
    }else if(kind==1){
        for(int i=0;i<12;++i)arc(d,181,i*30-92.5f,i*30-87.5f,17,shade);
        if(!aod){arc(d,212,-90,270,2,DIM);arc(d,212,-90,-90+minutes*6,3,WHITE);dot(d,233+sinf(sa)*212,233-cosf(sa)*212,9,COL_RED);
            for(int i=0;i<60;++i)if(i%5)dot(d,233+sinf(i*PI/30)*206,233-cosf(i*PI/30)*206,2.2f,0x56565e);
            arc(d,40,0,360,1,0x1d1d21);text(d,"soRound",233,130,120,14,GRAY,0);
            char day[8],value[16];strftime(day,sizeof day,"%a",&data->time);strftime(value,sizeof value,"%d %b",&data->time);
            for(char *p=day;*p;++p)*p=(char)toupper((unsigned char)*p);
            for(char *p=value;*p;++p)*p=(char)toupper((unsigned char)*p);
            text(d,day,233,300,96,14,GRAY,0);text(d,value,233,325,120,18,WHITE,0);hand_battery(d,data,354);}
        needle(d,ha,113,16,shade,true,false);needle(d,ma,171,5,shade,false,false);dot(d,233+sinf(ma)*171,233-cosf(ma)*171,7,aod?shade:COL_RED);
        dot(d,233,233,16,0);arc(d,7,0,360,2,shade);dot(d,233,233,4,aod?shade:COL_RED);
    }else if(kind==2){
        if(!aod)for(int i=0;i<60;++i)if(i%5){float a=i*PI/30;bar(d,233+sinf(a)*207,233-cosf(a)*207,233+sinf(a)*212,233-cosf(a)*212,1.2f,0x62626a);}
        for(int i=1;i<=12;++i){float a=i*PI/6;char value[4];snprintf(value,sizeof value,"%d",i);text(d,value,(int)lroundf(233+sinf(a)*179),(int)lroundf(233-cosf(a)*179),52,i%3?24:32,shade,0);}
        if(!aod){hand_date(d,&data->time,151,14);for(int i=0;i<12;++i){float a=i*PI/6;bar(d,233+sinf(a)*26,321-cosf(a)*26,233+sinf(a)*30,321-cosf(a)*30,1,0x73737b);}
            line(d,233,321,233+sinf(sa)*23,321-cosf(sa)*23,1.5f,COL_RED);dot(d,233,321,4,COL_RED);text(d,"soRound",233,371,120,14,GRAY,0);}
        leaf(d,ha,119,12,shade);leaf(d,ma,167,6,shade);dot(d,233,233,10,shade);dot(d,233,233,4,0);
    }else if(kind==3){
        arc(d,103,0,360,1,aod?0x353539:0x46464d);
        for(int i=0;i<60;++i){if(aod&&i%5)continue;float a=i*PI/30;dot(d,233+sinf(a)*207,233-cosf(a)*207,i%5?2.2f:6.4f,i%5?0x6b6b73:shade);}
        if(!aod){hand_date(d,&data->time,83,14);hand_battery(d,data,372);dot(d,233+sinf(sa)*218,233-cosf(sa)*218,7,COL_RED);}
        float hx=233+sinf(ha)*103,hy=233-cosf(ha)*103,mx=233+sinf(ma)*187,my=233-cosf(ma)*187;
        line(d,233,233,hx,hy,2,aod?shade:COL_RED);dot(d,hx,hy,38,aod?shade:COL_RED);dot(d,hx,hy,8,0);
        bar(d,233,233,mx,my,4,shade);dot(d,mx,my,12,shade);dot(d,mx,my,4,0);dot(d,233,233,18,0);dot(d,233,233,6,shade);
    }else if(kind==4){
        for(int i=0;i<60;++i){if(aod&&i%15)continue;float a=i*PI/30,s=sinf(a),c=-cosf(a),m=fmaxf(fabsf(s),fabsf(c));
            float outer=156/m,inner=(i%15==0?139:i%5==0?148:152)/m;
            bar(d,233+s*inner,233+c*inner,233+s*outer,233+c*outer,i%15==0?14:i%5==0?4:1,i%15==0?shade:GRAY);}
        if(!aod){bar(d,99,99,367,99,1,0x27272d);bar(d,367,99,367,367,1,0x27272d);bar(d,367,367,99,367,1,0x27272d);bar(d,99,367,99,99,1,0x27272d);
            text(d,"soRound",233,152,120,14,GRAY,0);char value[16];strftime(value,sizeof value,"%a %d",&data->time);for(char *p=value;*p;++p)*p=(char)toupper((unsigned char)*p);
            text(d,value,233,309,160,18,shade,0);strftime(value,sizeof value,"%B",&data->time);for(char *p=value;*p;++p)*p=(char)toupper((unsigned char)*p);text(d,value,233,336,180,14,GRAY,0);
            dot(d,233+sinf(sa)*207,233-cosf(sa)*207,5,COL_RED);}
        needle(d,ha,101,18,shade,false,true);needle(d,ma,169,6,aod?shade:COL_RED,false,true);
        bar(d,225,233,241,233,16,0);bar(d,230,233,236,233,6,shade);
    }else{
        for(int i=0;i<60;++i){float a=i*PI/30,r=i%5?216:192,x=233+sinf(a)*r,y=233-cosf(a)*r;
            if(i%5){if(!aod)dot(d,x,y,3.2f,0x5a5a63);}else for(int j=0;j<4;++j)dot(d,x+(j%2?4:-4),y+(j/2?4:-4),5.6f,shade);}
        if(!aod){char day[4],month[8];snprintf(day,sizeof day,"%02d",data->time.tm_mday);matrix(d,day,233,334,5,3);
            strftime(month,sizeof month,"%b",&data->time);for(char *p=month;*p;++p)*p=(char)toupper((unsigned char)*p);text(d,month,233,366,100,14,GRAY,0);
            dot(d,233+sinf(sa)*216,233-cosf(sa)*216,9,COL_RED);}
        line(d,233,233,233+sinf(ha)*102,233-cosf(ha)*102,16,shade);line(d,233,233,233+sinf(ma)*169,233-cosf(ma)*169,6,aod?shade:COL_RED);
        dot(d,233,233,16,0);dot(d,233,233,8,shade);
    }
}
void watchface_render(lv_layer_t *layer,const lv_area_t *area,const watchface_data_t *data,int index,bool preview) {
    draw_t d={.layer=layer,.x=area->x1,.y=area->y1,.size=lv_area_get_width(area),.preview=preview};
    if(index>=WATCHFACE_LEGACY_COUNT&&index<WATCHFACE_COUNT){hand_face(&d,data,index-WATCHFACE_LEGACY_COUNT);return;}
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
