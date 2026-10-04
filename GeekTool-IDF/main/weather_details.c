// Native inertial scrolling; four fixed drawing surfaces, no per-frame object creation.
#include "weather_details.h"
#include "weather_artwork.h"
#include "settings.h"
#include "lvgl_compat.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(font_wx_detail_16);
LV_FONT_DECLARE(font_wx_detail_20);
#define W 466
#define IVORY 0xf2eee6
#define MUTED 0x90999f
#define BLUE 0xa6c6d0
#define AMBER 0xe4b967
#define LINE 0x27333a
#define PANEL 0x0b1013
#define PI 3.14159265f
typedef struct {lv_layer_t *layer;lv_obj_t *obj;int x,y;lv_opa_t opa;} paint_t;
static const char *words(const char *en,const char *zh) {return settings_lang()?zh:en;}
static void text(paint_t *p,const char *s,int cx,int cy,const lv_font_t *font,uint32_t color) {
    lv_point_t size;lv_text_get_size(&size,s,font,0,0,450,LV_TEXT_FLAG_NONE);
    lv_area_t a={p->x+cx-size.x/2,p->y+cy-size.y/2,p->x+cx-size.x/2+size.x-1,p->y+cy-size.y/2+size.y-1};
    lv_draw_label_dsc_t d;lv_draw_label_dsc_init(&d);d.base.obj=p->obj;
    d.font=font;d.color=lv_color_hex(color);d.opa=p->opa;d.text=s;d.text_local=true;
    lv_draw_label(p->layer,&d,&a);
}
static void line(paint_t *p,int x1,int y1,int x2,int y2,int width,uint32_t color) {
    lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);d.base.obj=p->obj;
    d.p1=(lv_point_precise_t){p->x+x1,p->y+y1};d.p2=(lv_point_precise_t){p->x+x2,p->y+y2};
    d.width=width;d.color=lv_color_hex(color);d.opa=p->opa;d.round_start=d.round_end=true;lv_draw_line(p->layer,&d);
}
static void rect(paint_t *p,int x,int y,int width,int height,int radius,uint32_t color) {
    lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.base.obj=p->obj;d.bg_color=lv_color_hex(color);d.bg_opa=p->opa;d.radius=radius;
    lv_area_t a={p->x+x,p->y+y,p->x+x+width-1,p->y+y+height-1};lv_draw_rect(p->layer,&d,&a);
}
static void dot(paint_t *p,int x,int y,int radius,uint32_t color) {rect(p,x-radius,y-radius,radius*2,radius*2,LV_RADIUS_CIRCLE,color);}
static void arc(paint_t *p,int cx,int cy,float radius,float from,float to,int width,uint32_t color) {
    // One native arc task keeps the edge smooth and avoids dozens of line tasks per frame.
    if(to<=from)return;
    lv_draw_arc_dsc_t d;lv_draw_arc_dsc_init(&d);d.base.obj=p->obj;
    d.center=(lv_point_t){p->x+cx,p->y+cy};d.radius=(uint16_t)lroundf(radius+width*.5f);
    d.start_angle=(int)lroundf(from+360)%360;d.end_angle=(int)lroundf(to+360)%360;
    d.width=width;d.color=lv_color_hex(color);d.opa=p->opa;d.rounded=true;lv_draw_arc(p->layer,&d);
}
static void number(char *out,size_t size,float value,int decimals) {
    if(!isfinite(value))snprintf(out,size,"--");else snprintf(out,size,decimals?"%.1f":"%.0f",value);
}
static void metric(paint_t *p,int x,int y,const char *caption,float value,int decimals,const char *unit,uint32_t color) {
    rect(p,x,y,104,94,16,PANEL);
    text(p,caption,x+52,y+19,&font_wx_detail_16,MUTED);
    char value_text[24];number(value_text,sizeof value_text,value,decimals);
    text(p,value_text,x+52,y+47,&font_wx_detail_20,color);
    text(p,unit,x+52,y+74,&font_wx_detail_16,MUTED);
}
static void current_draw(paint_t *p,const weather_data_t *d) {
    text(p,words("CURRENT DETAILS","此刻详情"),233,110,&font_wx_detail_20,IVORY);
    metric(p,65,140,words("Feels like","体感"),d->apparent,1,"°",AMBER);
    metric(p,181,140,words("Rainfall","实时降水"),d->precipitation,1,"mm",BLUE);
    metric(p,297,140,words("Wind","风速"),d->wind,1,"m/s",IVORY);
    metric(p,65,246,words("Gusts","阵风"),d->gust,1,"m/s",IVORY);
    metric(p,181,246,words("Cloud","云量"),d->cloud,0,"%",BLUE);
    metric(p,297,246,words("Humidity","湿度"),(float)d->humidity,0,"%",IVORY);
    char pressure[24],visibility[24],value[16];
    number(value,sizeof value,d->pressure,0);snprintf(pressure,sizeof pressure,"%s hPa",value);
    number(value,sizeof value,d->visibility/1000,1);snprintf(visibility,sizeof visibility,"%s km",value);
    text(p,words("Pressure","海平面气压"),140,363,&font_wx_detail_16,MUTED);
    text(p,pressure,140,388,&font_wx_detail_20,IVORY);
    text(p,words("Visibility","能见度"),321,363,&font_wx_detail_16,MUTED);
    text(p,visibility,321,388,&font_wx_detail_20,IVORY);
    static const char *en[]={"N","NE","E","SE","S","SW","W","NW"};
    static const char *zh[]={"北","东北","东","东南","南","西南","西","西北"};
    char foot[64];
    if(isfinite(d->direction)) {
        unsigned index=((unsigned)lroundf(d->direction/45))%8;
        snprintf(foot,sizeof foot,"%s %s  ·  %s %s",words("From","来向"),settings_lang()?zh[index]:en[index],
                 words("Updated","更新"),d->updated[0]?d->updated:"--");
    } else snprintf(foot,sizeof foot,"%s %s",words("Updated","更新"),d->updated[0]?d->updated:"--");
    text(p,foot,233,420,&font_wx_detail_16,MUTED);
}
static void hourly_draw(paint_t *p,const weather_data_t *d,float reveal) {
    text(p,words("NEXT 12 HOURS","未来12小时"),233,109,&font_wx_detail_20,IVORY);
    float low=100,high=-100;unsigned valid=0;
    for(unsigned i=0;i<d->hour_count;++i) if(isfinite(d->hours[i].temperature)) {
        low=fminf(low,d->hours[i].temperature);high=fmaxf(high,d->hours[i].temperature);++valid;
    }
    if(!valid) {text(p,words("Hourly forecast unavailable","暂无小时预报"),233,233,&font_wx_detail_20,MUTED);return;}
    char range[32];snprintf(range,sizeof range,"%.0f°  /  %.0f°",low,high);
    text(p,range,233,150,&font_wx_detail_20,IVORY);
    float span=fmaxf(2,high-low);low-=(span-(high-low))/2;
    for(int y=191;y<=251;y+=30)line(p,73,y,393,y,1,LINE);
    int previous_x=0,previous_y=0;bool previous=false;
    for(unsigned i=0;i<d->hour_count;++i) {
        const weather_hour_t *h=&d->hours[i];int x=73+(int)(i*320/fmaxf(1,d->hour_count-1));
        if(!isfinite(h->temperature) || i>reveal*(d->hour_count-1)) {previous=false;continue;}
        int y=251-(int)lroundf((h->temperature-low)/span*60);
        if(previous)line(p,previous_x,previous_y,x,y,3,BLUE);
        dot(p,x,y,i==0?4:2,i==0?AMBER:BLUE);previous_x=x;previous_y=y;previous=true;
    }
    for(unsigned k=0;k<4;++k) {
        unsigned i=k*(d->hour_count-1)/3;
        text(p,d->hours[i].time,81+(int)k*101,278,&font_wx_detail_16,MUTED);
    }
    text(p,words("RAIN CHANCE","降水概率"),233,311,&font_wx_detail_16,MUTED);
    line(p,73,386,393,386,1,LINE);
    float rain=0,peak=NAN;bool has_rain=false;
    for(unsigned i=0;i<d->hour_count;++i) {
        const weather_hour_t *h=&d->hours[i];int x=73+(int)(i*320/fmaxf(1,d->hour_count-1));
        if(isfinite(h->probability)) {
            int height=(int)lroundf(h->probability*.58f*reveal);
            if(height>0)rect(p,x-6,386-height,12,height,4,BLUE);
            peak=isfinite(peak)?fmaxf(peak,h->probability):h->probability;
        }
        if(isfinite(h->precipitation)){rain+=h->precipitation;has_rain=true;}
    }
    char a[12],b[12],foot[80];number(a,sizeof a,peak,0);number(b,sizeof b,has_rain?rain:NAN,1);
    snprintf(foot,sizeof foot,"%s %s%%  ·  %s %s mm",words("Peak","最高"),a,words("Total","累计"),b);
    text(p,foot,233,410,&font_wx_detail_16,MUTED);
}
static void mini_icon(paint_t *p,int code,int cx,int cy) {
    const weather_artwork_t *art=weather_artwork_for(code,true);
    if(!art) {for(int k=-1;k<=1;++k)dot(p,cx+k*6,cy,2,MUTED);return;}
    lv_draw_image_dsc_t d;lv_draw_image_dsc_init(&d);d.base.obj=p->obj;d.src=art->image;d.opa=p->opa;
    d.scale_x=d.scale_y=56;d.pivot=(lv_point_t){0,0};
    int scaled_w=(int)(art->image->header.w*56/256),scaled_h=(int)(art->image->header.h*56/256);
    lv_area_t a={p->x+cx-scaled_w/2,p->y+cy-scaled_h/2,p->x+cx-scaled_w/2+(int)art->image->header.w-1,
                 p->y+cy-scaled_h/2+(int)art->image->header.h-1};
    lv_draw_image(p->layer,&d,&a);
}
static void daily_draw(paint_t *p,const weather_data_t *d,float reveal) {
    text(p,words("5-DAY OUTLOOK","未来五天"),233,108,&font_wx_detail_20,IVORY);
    if(!d->day_count) {text(p,words("Daily forecast unavailable","暂无逐日预报"),233,233,&font_wx_detail_20,MUTED);return;}
    float low=100,high=-100;
    for(unsigned i=0;i<d->day_count;++i) {if(isfinite(d->days[i].low))low=fminf(low,d->days[i].low);if(isfinite(d->days[i].high))high=fmaxf(high,d->days[i].high);}
    for(unsigned i=0;i<d->day_count;++i) {
        const weather_day_t *day=&d->days[i];int y=157+(int)i*50;
        char date[8],a[20],b[20];snprintf(date,sizeof date,"%.2s/%.2s",day->date+5,day->date+8);
        text(p,i==0?words("Today","今天"):date,94,y,&font_wx_detail_16,i==0?IVORY:MUTED);
        mini_icon(p,day->code,151,y);
        number(a,sizeof a,day->low,0);strncat(a,"°",sizeof a-strlen(a)-1);text(p,a,198,y,&font_wx_detail_20,MUTED);
        number(b,sizeof b,day->high,0);strncat(b,"°",sizeof b-strlen(b)-1);text(p,b,302,y,&font_wx_detail_20,IVORY);
        line(p,220,y,277,y,4,LINE);
        if(isfinite(day->low)&&isfinite(day->high)) {
            float span=fmaxf(1,high-low);int start=220+(int)((day->low-low)/span*57),end=220+(int)((day->high-low)/span*57);
            line(p,start,y,start+(int)((end-start)*reveal),y,4,AMBER);
        }
        number(a,sizeof a,day->probability,0);strncat(a,"%",sizeof a-strlen(a)-1);text(p,a,358,y-9,&font_wx_detail_16,BLUE);
        number(b,sizeof b,day->precipitation,1);strncat(b,"mm",sizeof b-strlen(b)-1);text(p,b,358,y+13,&font_wx_detail_16,MUTED);
    }
    text(p,words("Daily rain chance / total","每日降水概率与雨量"),233,408,&font_wx_detail_16,MUTED);
}
static int minutes(const char *s) {return s[0]?(s[0]-'0')*600+(s[1]-'0')*60+(s[3]-'0')*10+s[4]-'0':-1;}
static void daylight_draw(paint_t *p,const weather_data_t *d,float reveal) {
    text(p,words("DAYLIGHT & UV","日光与 UV"),233,110,&font_wx_detail_20,IVORY);
    const weather_day_t *today=&d->days[0];
    arc(p,233,304,129,180,360,3,LINE);line(p,92,304,374,304,1,LINE);
    int rise=minutes(today->sunrise),set=minutes(today->sunset),now=minutes(d->updated);
    if(d->day_count && rise>=0 && set>rise && now>=0) {
        float fraction=fminf(1,fmaxf(0,(now-rise)/(float)(set-rise)));
        arc(p,233,304,129,180,180+180*fraction*reveal,3,AMBER);
        float angle=(180+180*fraction)*PI/180;
        dot(p,233+(int)lroundf(cosf(angle)*129),304+(int)lroundf(sinf(angle)*129),6,d->is_day?AMBER:BLUE);
    }
    text(p,words("TODAY'S MAX UV","今日最高 UV"),233,231,&font_wx_detail_16,MUTED);
    char uv[16];number(uv,sizeof uv,d->day_count?today->uv:NAN,1);
    text(p,uv,233,269,&lv_font_montserrat_40,IVORY);
    text(p,today->sunrise[0]?today->sunrise:"--",112,329,&font_wx_detail_20,IVORY);
    text(p,today->sunset[0]?today->sunset:"--",354,329,&font_wx_detail_20,IVORY);
    text(p,words("Sunrise","日出"),112,357,&font_wx_detail_16,MUTED);
    text(p,words("Sunset","日落"),354,357,&font_wx_detail_16,MUTED);
    char duration[64];
    if(d->day_count && isfinite(today->daylight)) {
        int total=(int)lroundf(today->daylight/60);
        snprintf(duration,sizeof duration,"%s %dh %02dm",words("Daylight","日照时长"),total/60,total%60);
    } else snprintf(duration,sizeof duration,"%s --",words("Daylight","日照时长"));
    text(p,duration,233,395,&font_wx_detail_16,MUTED);
    text(p,"Open-Meteo",233,421,&font_wx_detail_16,MUTED);
}
static void section_draw(lv_event_t *e) {
    weather_details_t *v=lv_event_get_user_data(e);lv_obj_t *o=lv_event_get_target_obj(e);
    unsigned index=0;while(index<4 && v->sections[index]!=o)++index;if(index==4)return;
    lv_area_t a;lv_obj_get_coords(o,&a);
    paint_t p={lv_event_get_layer(e),o,a.x1,a.y1,(lv_opa_t)(80+175*v->reveal[index])};
    if(!v->available) {text(&p,words("Weather details unavailable","天气详情暂不可用"),233,233,&font_wx_detail_20,MUTED);return;}
    p.y+=(int)lroundf((1-v->reveal[index])*12); // Scroll-coupled entry, no forever-running animation.
    if(index==0)current_draw(&p,&v->data);
    else if(index==1)hourly_draw(&p,&v->data,v->reveal[index]);
    else if(index==2)daily_draw(&p,&v->data,v->reveal[index]);
    else daylight_draw(&p,&v->data,v->reveal[index]);
}
static void indicator_draw(lv_event_t *e) {
    weather_details_t *v=lv_event_get_user_data(e);if(!v->indicator_opa)return;
    lv_area_t a;lv_obj_get_coords(v->indicator,&a);paint_t p={lv_event_get_layer(e),v->indicator,a.x1,a.y1,v->indicator_opa};
    float total=lv_obj_get_scroll_y(v->scroll)+lv_obj_get_scroll_bottom(v->scroll);
    float fraction=total>0?lv_obj_get_scroll_y(v->scroll)/total:0;fraction=fminf(1,fmaxf(0,fraction));
    float thumb=fmaxf(14,104*W/(total+W));
    arc(&p,233,233,214,-52,52,2,LINE);
    arc(&p,233,233,214,-52+fraction*(104-thumb),-52+fraction*(104-thumb)+thumb,3,BLUE);
}
static void indicator_invalidate(weather_details_t *v) {
    lv_area_t a;lv_obj_get_coords(v->indicator,&a);
    lv_area_t dirty={a.x1+363,a.y1+60,a.x1+452,a.y1+406};lv_obj_invalidate_area(v->indicator,&dirty);
}
static void indicator_fade(void *obj,int32_t value) {weather_details_t *v=obj;v->indicator_opa=(uint8_t)value;indicator_invalidate(v);}
static void top_mask(lv_event_t *e) {
    weather_details_t *v=lv_event_get_user_data(e);if(lv_obj_get_scroll_y(v->scroll)<=0)return;
    lv_area_t a;lv_obj_get_coords(v->scroll,&a);paint_t p={lv_event_get_layer(e),v->scroll,a.x1,a.y1,LV_OPA_COVER};
    rect(&p,0,0,466,91,0,0); // Keep the existing fixed header legible while content passes beneath it.
}
static void scroll_event(lv_event_t *e) {
    weather_details_t *v=lv_event_get_user_data(e);lv_event_code_t code=lv_event_get_code(e);
    if(code==LV_EVENT_SCROLL) {
        int y=lv_obj_get_scroll_y(v->scroll);
        for(int i=0;i<4;++i) {
            float reveal=fminf(1,fmaxf(0,(W-fabsf((float)((i+1)*W-y)))/400));
            if(reveal!=v->reveal[i]) {v->reveal[i]=reveal;lv_obj_invalidate(v->sections[i]);}
        }
        indicator_invalidate(v);
    } else if(code==LV_EVENT_SCROLL_BEGIN) {
        lv_anim_delete(v,indicator_fade);v->indicator_opa=255;indicator_invalidate(v);
    } else if(code==LV_EVENT_SCROLL_END) {
        lv_anim_t anim;lv_anim_init(&anim);lv_anim_set_var(&anim,v);lv_anim_set_exec_cb(&anim,indicator_fade);
        lv_anim_set_values(&anim,v->indicator_opa,0);lv_anim_set_delay(&anim,650);lv_anim_set_duration(&anim,220);lv_anim_start(&anim);
    }
}
static lv_obj_t *surface(lv_obj_t *parent,int y,int height) {
    lv_obj_t *o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,0,y);lv_obj_set_size(o,W,height);
    ui_obj_set_scrollable(o,false);ui_obj_set_event_bubble(o,true);ui_obj_set_gesture_bubble(o,true);return o;
}
void weather_details_create(weather_details_t *v,lv_obj_t *parent) {
    memset(v,0,sizeof *v);
    v->scroll=surface(parent,0,W);ui_obj_set_scrollable(v->scroll,true);
    lv_obj_set_scroll_dir(v->scroll,LV_DIR_VER);lv_obj_set_scrollbar_mode(v->scroll,LV_SCROLLBAR_MODE_OFF);
    ui_obj_set_scroll_chain_hor(v->scroll,false);ui_obj_set_scroll_chain_ver(v->scroll,false);
    ui_obj_set_scroll_elastic(v->scroll,false);
    ui_obj_set_scroll_momentum(v->scroll,true);
    v->hero=surface(v->scroll,0,W);
    for(int i=0;i<4;++i) {
        v->sections[i]=surface(v->scroll,(i+1)*W,W);
        lv_obj_add_event_cb(v->sections[i],section_draw,LV_EVENT_DRAW_MAIN,v);
    }
    v->indicator=surface(parent,0,W);ui_obj_set_clickable(v->indicator,false);
    lv_obj_add_event_cb(v->indicator,indicator_draw,LV_EVENT_DRAW_MAIN,v);
    lv_obj_add_event_cb(v->scroll,scroll_event,LV_EVENT_SCROLL,v);
    lv_obj_add_event_cb(v->scroll,scroll_event,LV_EVENT_SCROLL_BEGIN,v);
    lv_obj_add_event_cb(v->scroll,scroll_event,LV_EVENT_SCROLL_END,v);
    lv_obj_add_event_cb(v->scroll,top_mask,LV_EVENT_DRAW_POST,v);
}
void weather_details_show(weather_details_t *v,const weather_data_t *data,bool available) {
    v->available=available;if(available && data)v->data=*data;
    for(int i=0;i<4;++i)lv_obj_invalidate(v->sections[i]);
}
void weather_details_reset(weather_details_t *v) {
    // Stopping or resetting native scrolling can emit SCROLL_END and enqueue a fade. Delete it last.
    lv_obj_stop_scroll_anim(v->scroll);lv_obj_scroll_to_y(v->scroll,0,LV_ANIM_OFF);lv_anim_delete(v,indicator_fade);
    v->indicator_opa=0;v->available=false;memset(v->reveal,0,sizeof v->reveal);indicator_invalidate(v);
}
void weather_details_visibility(weather_details_t *v,bool visible) {
    if(!visible && v->scroll) {lv_obj_stop_scroll_anim(v->scroll);lv_anim_delete(v,indicator_fade);v->indicator_opa=0;indicator_invalidate(v);}
}
void weather_details_close(weather_details_t *v) {
    if(v->scroll)lv_obj_stop_scroll_anim(v->scroll);
    lv_anim_delete(v,indicator_fade);memset(v,0,sizeof *v);
}
