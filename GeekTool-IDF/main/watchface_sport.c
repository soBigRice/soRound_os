#include "watchface_sport.h"
#include "watchface_studio_draw.h"

#define SPORT_TAU 6.2831853071795864769f

typedef struct {float hour12,hour24,minute,second;} sport_time_t;

static sport_time_t progress(const watchface_data_t *data)
{
    float seconds=data->time.tm_sec/60.0f;
    float minutes=(data->time.tm_min+seconds)/60.0f;
    return (sport_time_t){((data->time.tm_hour%12)+minutes)/12.0f,
                          (data->time.tm_hour+minutes)/24.0f,minutes,seconds};
}

static wfs_point_t polar(float x,float y,float radius,float turn)
{
    return (wfs_point_t){x+sinf(turn*SPORT_TAU)*radius,y-cosf(turn*SPORT_TAU)*radius};
}

static void date(const wfs_draw_t *d,const watchface_data_t *data,float baseline)
{
    char text[32];wfs_date(data,text,sizeof text);wfs_text(d,text,233,baseline,12,0x8b969a,2);
}

static void ring(const wfs_draw_t *d,float x,float y,float radius,float width,
                  float fraction,uint32_t color,uint32_t track)
{
    wfs_circle(d,x,y,radius,width,track);
    // 从12点顺时针编码时间比例，零值保持空轨道，不冒充活动量。
    // Encode clock fractions clockwise from twelve; zero leaves an empty track rather than suggesting activity data.
    if(fraction<=0)return;
    fraction=fminf(fraction,1);
    wfs_arc(d,x,y,radius,-90,-90+360*fraction,width,color);
    wfs_point_t end=polar(x,y,radius,fraction);
    wfs_disc(d,x,y-radius,width/2,color);wfs_disc(d,end.x,end.y,width/2,color);
}

static void activity(const wfs_draw_t *d,const watchface_data_t *data,sport_time_t time)
{
    ring(d,233,233,204,22,time.hour12,0xf16b8d,0x341b26);
    ring(d,233,233,173,22,time.minute,0xb8e95c,0x26331b);
    ring(d,233,233,142,22,time.second,0x66cde3,0x182d35);
    wfs_text(d,"TIME / ACTIVITY",233,183,12,0x8b969a,2);
    char text[8];wfs_time(data,text,sizeof text);wfs_text(d,text,233,255,72,0xf0efe6,0);
    date(d,data,290);
    wfs_text(d,"H12",188,319,12,0xf16b8d,1);
    wfs_text(d,"M60",233,319,12,0xb8e95c,1);
    wfs_text(d,"S60",278,319,12,0x66cde3,1);
}

static wfs_point_t track_point(float radius,float fraction)
{
    const float top=178,bottom=288,straight=bottom-top;
    float quarter=SPORT_TAU*radius/4,total=4*quarter+2*straight;
    float distance=fraction*total,angle;
    if(distance<=quarter){angle=-SPORT_TAU/4+distance/radius;return (wfs_point_t){233+cosf(angle)*radius,top+sinf(angle)*radius};}
    distance-=quarter;
    if(distance<=straight)return (wfs_point_t){233+radius,top+distance};
    distance-=straight;
    if(distance<=2*quarter){angle=distance/radius;return (wfs_point_t){233+cosf(angle)*radius,bottom+sinf(angle)*radius};}
    distance-=2*quarter;
    if(distance<=straight)return (wfs_point_t){233-radius,bottom-distance};
    distance-=straight;angle=SPORT_TAU/2+distance/radius;
    return (wfs_point_t){233+cosf(angle)*radius,top+sinf(angle)*radius};
}

static void circuit(const wfs_draw_t *d,const watchface_data_t *data,sport_time_t time)
{
    const float fraction[]={time.hour12,time.minute,time.second};
    const uint32_t color[]={0xeeae6e,0x6dcee6,0xe3e7ce},track[]={0x382a21,0x193039,0x2d3029};
    wfs_text(d,"CIRCUIT",233,32,12,0xa7aca5,3);
    // 固定64段逐段绘制，局部只有两个点，不在任务栈上建立轨道数组。
    // Draw sixty-four bounded segments with two local points instead of a track array on the task stack.
    for(int lane=0;lane<3;lane++){
        float radius=126-lane*21;wfs_point_t previous=track_point(radius,0);
        for(int segment=1;segment<=64;segment++){
            float end=segment/64.0f;wfs_point_t next=track_point(radius,end);
            wfs_bar(d,previous.x,previous.y,next.x,next.y,12,track[lane]);
            if((segment-1)/64.0f<fraction[lane]){
                wfs_point_t filled=track_point(radius,fminf(end,fraction[lane]));
                wfs_bar(d,previous.x,previous.y,filled.x,filled.y,12,color[lane]);
            }
            previous=next;
        }
        wfs_point_t point=track_point(radius,fraction[lane]);wfs_disc(d,point.x,point.y,7,color[lane]);
        wfs_disc(d,point.x,point.y,2,0x101619);
    }
    char hour[3],minute[3];snprintf(hour,sizeof hour,"%02u",(unsigned)data->time.tm_hour%24u);
    snprintf(minute,sizeof minute,"%02u",(unsigned)data->time.tm_min%60u);
    wfs_text(d,"HOURS",233,144,12,color[0],2);wfs_text(d,hour,233,226,87,0xf0efe6,0);
    wfs_text(d,"MINUTES",233,247,12,color[1],2);wfs_text(d,minute,233,323,87,0xf0efe6,0);
    date(d,data,347);
}

static void matrix(const wfs_draw_t *d,const watchface_data_t *data,sport_time_t time)
{
    wfs_text(d,"HOURS / 24",233,81,12,0xcbaa89,3);
    for(int cell=0;cell<24;cell++)wfs_rect(d,73+(cell%12)*27,100+(cell/12)*29,23,17,4,
                                        cell<data->time.tm_hour?0xe6ad73:0x2e251e);
    char text[8];wfs_time(data,text,sizeof text);wfs_heavy_text(d,text,233,239,96,0xf0efe6,0);
    wfs_text(d,"MINUTES / 60",233,267,12,0x94c5b2,3);
    for(int cell=0;cell<60;cell++)wfs_rect(d,110+(cell%10)*25,282+(cell/10)*22,21,14,3,
                                        cell<data->time.tm_min?0x9fd1b7:0x1e3029);
    wfs_bar(d,170,435,296,435,2,0x313c40);
    wfs_bar(d,170,435,170+126*time.second,435,2,0xa2badd);
    wfs_point_t second={170+126*time.second,435};wfs_disc(d,second.x,second.y,3,0xa2badd);
}

static void vector(const wfs_draw_t *d,const watchface_data_t *data,sport_time_t time)
{
    wfs_circle(d,233,233,153,1,0x363a39);
    for(int hour=0;hour<12;hour++){
        wfs_point_t p=polar(233,233,205,hour/12.0f);
        if(hour%3)wfs_disc(d,p.x,p.y,2,0x657477);
        else{
            // 大时标位于分针圆点之外，午夜时不遮住读时标记。
            // Place cardinal marks outside the minute bead so midnight does not obscure them.
            wfs_point_t a=polar(233,233,198,hour/12.0f),b=polar(233,233,211,hour/12.0f);
            wfs_bar(d,a.x,a.y,b.x,b.y,4,0xc3cac4);
        }
    }
    wfs_point_t triangle[]={polar(233,233,143,time.hour12),polar(233,233,86,time.hour12-.034f),polar(233,233,86,time.hour12+.034f)};
    wfs_poly(d,triangle,3,0xc5eb77);
    wfs_point_t minute=polar(233,233,182,time.minute),tail=polar(233,233,28,time.minute+.5f);
    wfs_bar(d,tail.x,tail.y,minute.x,minute.y,4,0xe6e9de);wfs_disc(d,minute.x,minute.y,10,0xe6e9de);
    wfs_point_t second=polar(233,233,218,time.second);wfs_disc(d,second.x,second.y,4,0xeb9d75);
    wfs_disc(d,233,233,13,0xc5eb77);wfs_disc(d,233,233,6,0x11191a);
    wfs_text(d,"V E C T O R",233,292,12,0x879b9d,1);date(d,data,321);
}

static void duo(const wfs_draw_t *d,const watchface_data_t *data,sport_time_t time)
{
    ring(d,233,233,216,2,time.second,0xc3ccbf,0x2a3533);
    ring(d,171,191,108,18,time.hour24,0xeaa873,0x36291f);
    ring(d,291,286,108,18,time.minute,0x73cade,0x19323a);
    wfs_text(d,"DUO / TIME",233,43,12,0x8b969a,3);
    char hour[3],minute[3];snprintf(hour,sizeof hour,"%02u",(unsigned)data->time.tm_hour%24u);
    snprintf(minute,sizeof minute,"%02u",(unsigned)data->time.tm_min%60u);
    wfs_text(d,"H / 24",171,151,12,0xeaa873,2);wfs_text(d,hour,171,223,87,0xf0efe6,0);
    wfs_text(d,"M / 60",291,246,12,0x73cade,2);wfs_text(d,minute,291,318,87,0xf0efe6,0);
    date(d,data,433);
}

static void stride(const wfs_draw_t *d,const watchface_data_t *data,sport_time_t time)
{
    const float fraction[]={time.hour24,time.minute,time.second};
    const int values[]={data->time.tm_hour,data->time.tm_min,data->time.tm_sec};
    const uint32_t color[]={0xed6b91,0x79c9e6,0xc6e97c},track[]={0x321d27,0x1a2c36,0x2a321d};
    const char *const labels[]={"HOURS / 24","MINUTES / 60","SECONDS / 60"};
    wfs_text(d,"S T R I D E",233,53,12,0x8b969a,2);
    for(int lane=0;lane<3;lane++){
        float y=lane*114;char value[3];snprintf(value,sizeof value,"%02u",(unsigned)values[lane]%100u);
        wfs_text(d,labels[lane],151,113+y,12,color[lane],2);
        wfs_text(d,value,347,131+y,72,0xf0efe6,0);
        wfs_rect(d,84,141+y,298,30,15,track[lane]);
        float filled=298*fraction[lane];
        if(filled>=1)wfs_rect(d,84,141+y,filled,30,15,color[lane]);
    }
    date(d,data,430);
}

void watchface_sport_render(lv_layer_t *layer,const lv_area_t *area,
                            const watchface_data_t *data,int kind,bool preview)
{
    static const char *const names[]={"ACTIVITY","CIRCUIT","MATRIX","VECTOR","DUO","STRIDE"};
    if(!data || !area || kind<0 || kind>=6)return;
    wfs_draw_t d=wfs_make(layer,area,preview);
    if(data->aod){wfs_aod(&d,data,names[kind]);return;}
    sport_time_t time=progress(data);
    switch(kind){
    case 0:activity(&d,data,time);break;
    case 1:circuit(&d,data,time);break;
    case 2:matrix(&d,data,time);break;
    case 3:vector(&d,data,time);break;
    case 4:duo(&d,data,time);break;
    case 5:stride(&d,data,time);break;
    }
}
