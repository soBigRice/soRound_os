#include "app.h"
#include "lvgl_compat.h"
#include "zodiac_data.h"
#include "settings.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

LV_FONT_DECLARE(font_answer_32);
LV_FONT_DECLARE(font_location_24);
static lv_obj_t *g_reading,*g_picker,*g_title,*g_date,*g_category,*g_score,*g_body,*g_scroll,*g_action;
static bool s_visible,s_waiting,s_ready,s_selecting,s_pending_start;
static unsigned s_sign,s_category;
static uint32_t s_token,s_started;
static int s_day;
static zodiac_data_t s_data;
static char s_copy[2048];
static const char *words(const char *en,const char *zh){return settings_lang()?zh:en;}
static lv_obj_t *block(lv_obj_t *p,int x,int y,int w,int h) {
    lv_obj_t *o=lv_obj_create(p);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    ui_obj_set_scrollable(o,false);return o;
}
static lv_obj_t *label(lv_obj_t *p,const char *value,int x,int y,int w,const lv_font_t *font,uint32_t color) {
    lv_obj_t *o=lv_label_create(p);lv_obj_set_pos(o,x,y);lv_obj_set_width(o,w);
    lv_obj_set_style_text_font(o,font,0);lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);lv_label_set_text(o,value);return o;
}
static lv_obj_t *button(lv_obj_t *p,const char *value,int x,int y,int w,lv_event_cb_t cb,void *arg) {
    lv_obj_t *o=block(p,x,y,w,40);lv_obj_set_style_bg_color(o,lv_color_hex(0x202025),0);
    lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_radius(o,20,0);
    ui_obj_set_clickable(o,true);lv_obj_add_event_cb(o,cb,LV_EVENT_CLICKED,arg);
    lv_obj_t *t=label(o,value,0,5,w,&font_location_24,COL_TXT);ui_obj_set_event_bubble(t,true);return o;
}
static int today(void) {
    time_t now=time(NULL);struct tm day;localtime_r(&now,&day);
    return (day.tm_year+1900)*10000+(day.tm_mon+1)*100+day.tm_mday;
}
static bool glyphs(const char *text) {
    const unsigned char *p=(const unsigned char *)text;
    while(*p) {
        uint32_t cp=*p++;unsigned more=0;
        if(cp>=0xf0){cp&=7;more=3;}else if(cp>=0xe0){cp&=15;more=2;}else if(cp>=0xc0){cp&=31;more=1;}
        for(unsigned i=0;i<more;++i)cp=(cp<<6)|(*p++&63);
        if(cp=='\n')continue;
        lv_font_glyph_dsc_t d;
        if(!lv_font_get_glyph_dsc(&font_answer_32,&d,cp,0)||d.is_placeholder)return false;
    }
    return true;
}
static void render(void) {
    if(!s_waiting)lv_obj_remove_state(g_action,LV_STATE_DISABLED);
    static const char *en[]={"Overall","Love","Work","Wealth","Health"};
    static const char *zh[]={"综合运势","爱情运势","事业运势","财富运势","健康运势"};
    lv_label_set_text(g_category,settings_lang()?zh[s_category]:en[s_category]);
    char rating[32];uint8_t n=s_data.score[s_category];
    snprintf(rating,sizeof rating,n<=5?"%u / 5":"-- / 5",n<=5?n:0);
    lv_label_set_text(g_score,s_ready?rating:"");
    if(!s_ready)return;
    char date[96];snprintf(date,sizeof date,settings_lang()?"%s":"%s · Chinese original",s_data.date);
    lv_label_set_text(g_date,date);
    if(s_category==0) {
        // Keep the provider's Chinese original in both UI languages; never invent a translation.
        snprintf(s_copy,sizeof s_copy,"%s\n%s\n\n幸运颜色：%s\n幸运数字：%s\n贵人星座：%s\n\n宜：%s\n忌：%s",
            s_data.comment,s_data.body[0],s_data.color[0]?s_data.color:"--",s_data.number[0]?s_data.number:"--",
            s_data.partner[0]?s_data.partner:"--",s_data.good[0]?s_data.good:"--",s_data.avoid[0]?s_data.avoid:"--");
    }else snprintf(s_copy,sizeof s_copy,"%s",s_data.body[s_category]);
    lv_label_set_text(g_body,!s_copy[0]?words("NO CONTENT","暂无内容"):
        glyphs(s_copy)?s_copy:words("CONTENT UNAVAILABLE","内容暂不支持"));
    lv_obj_scroll_to_y(g_scroll,0,LV_ANIM_OFF);
}
static void status(zodiac_state_t state) {
    if(s_waiting)lv_obj_add_state(g_action,LV_STATE_DISABLED);
    else lv_obj_remove_state(g_action,LV_STATE_DISABLED);
    lv_label_set_text(g_date,words("DAILY HOROSCOPE · 中文原文","每日更新 · 联网获取"));
    lv_label_set_text(g_score,"");
    lv_label_set_text(g_body,state==ZODIAC_LOADING?words("FETCHING…","获取中…"):
        state==ZODIAC_OFFLINE?words("NO NETWORK","未连接网络"):
        state==ZODIAC_BUSY?words("REQUEST ENDING\nRETRY SOON","请求正在结束\n稍后重试"):words("FETCH FAILED","获取失败"));
    lv_obj_scroll_to_y(g_scroll,0,LV_ANIM_OFF);
}
static void refresh(lv_event_t *e) {
    (void)e;if(s_waiting||!s_visible)return;
    s_ready=false;s_started=lv_tick_get();s_day=today();
    zodiac_state_t state=zodiac_fetch_begin(s_sign,&s_token);
    s_pending_start=state==ZODIAC_BUSY;s_waiting=state==ZODIAC_LOADING||s_pending_start;
    status(state);
}
static void choose(lv_event_t *e) {
    unsigned sign=(unsigned)(uintptr_t)lv_event_get_user_data(e);
    zodiac_fetch_cancel();s_waiting=false;s_sign=sign;s_category=0;s_selecting=false;
    lv_label_set_text(g_title,settings_lang()?ZODIAC_ZH[s_sign]:ZODIAC_EN[s_sign]);
    ui_obj_set_hidden(g_picker,true);ui_obj_set_hidden(g_reading,false);render();refresh(NULL);
}
static void picker(lv_event_t *e) {
    (void)e;s_selecting=true;ui_obj_set_hidden(g_reading,true);ui_obj_set_hidden(g_picker,false);
}
static void category(lv_event_t *e) {
    int step=(int)(intptr_t)lv_event_get_user_data(e);
    s_category=(unsigned)((int)s_category+step+5)%5;render();
}
static void zodiac_tick(void) {
    if(!s_visible)return;
    if(s_waiting) {
        if(s_pending_start) {
            zodiac_state_t next=zodiac_fetch_begin(s_sign,&s_token);
            if(next==ZODIAC_BUSY&&lv_tick_get()-s_started<8500)return;
            s_pending_start=false;s_waiting=next==ZODIAC_LOADING;
            if(s_waiting)s_started=lv_tick_get();
            status(next==ZODIAC_BUSY?ZODIAC_FAILED:next);return;
        }
        zodiac_state_t state=zodiac_fetch_poll(s_token,&s_data);
        if(state==ZODIAC_READY){s_ready=true;s_waiting=false;render();}
        else if(state!=ZODIAC_LOADING||lv_tick_get()-s_started>=8500) {
            zodiac_fetch_cancel();s_waiting=false;status(ZODIAC_FAILED);
        }
    }else if(!s_selecting&&s_ready&&s_day!=today())refresh(NULL);
}
static bool zodiac_back(void) {
    if(!s_selecting)return false;
    s_selecting=false;ui_obj_set_hidden(g_picker,true);ui_obj_set_hidden(g_reading,false);return true;
}
static void zodiac_visibility(bool visible){s_visible=visible;}
static void zodiac_enter(lv_obj_t *parent) {
    s_visible=true;s_waiting=s_ready=s_selecting=s_pending_start=false;s_category=0;
    g_reading=block(parent,0,100,466,356);
    g_title=label(g_reading,settings_lang()?ZODIAC_ZH[s_sign]:ZODIAC_EN[s_sign],83,0,300,&font_location_24,COL_TXT);
    ui_obj_set_clickable(g_title,true);lv_obj_add_event_cb(g_title,picker,LV_EVENT_CLICKED,NULL);
    label(g_reading,words("EDIT","切换"),326,2,58,&font_location_24,COL_RED);
    g_date=label(g_reading,"",73,37,320,&font_location_24,COL_TXT2);
    button(g_reading,"<",92,77,44,category,(void *)(intptr_t)-1);
    button(g_reading,">",330,77,44,category,(void *)(intptr_t)1);
    g_category=label(g_reading,"",148,81,170,&font_location_24,COL_TXT);
    g_score=label(g_reading,"",178,112,110,&font_location_24,COL_RED);
    g_scroll=block(g_reading,92,146,282,148);ui_obj_set_scrollable(g_scroll,true);
    lv_obj_set_style_bg_color(g_scroll,lv_color_hex(COL_TXT),LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(g_scroll,LV_OPA_50,LV_PART_SCROLLBAR);lv_obj_set_style_width(g_scroll,3,LV_PART_SCROLLBAR);
    lv_obj_set_scroll_dir(g_scroll,LV_DIR_VER);lv_obj_set_scrollbar_mode(g_scroll,LV_SCROLLBAR_MODE_AUTO);
    ui_obj_set_scroll_elastic(g_scroll,false);ui_obj_set_scroll_chain_ver(g_scroll,false);
    g_body=label(g_scroll,"",0,0,276,&font_answer_32,COL_TXT);
    lv_obj_set_style_text_align(g_body,LV_TEXT_ALIGN_LEFT,0);lv_obj_set_style_text_line_space(g_body,7,0);
    g_action=button(g_reading,words("REFRESH","刷新运势"),138,297,190,refresh,NULL);
    g_picker=block(parent,83,107,300,291);
    label(g_picker,words("CHOOSE YOUR SIGN","选择你的星座"),0,0,300,&font_location_24,COL_TXT);
    lv_obj_t *list=block(g_picker,7,43,286,238);ui_obj_set_scrollable(list,true);
    lv_obj_set_scroll_dir(list,LV_DIR_VER);lv_obj_set_scrollbar_mode(list,LV_SCROLLBAR_MODE_AUTO);
    ui_obj_set_scroll_elastic(list,false);ui_obj_set_scroll_chain_ver(list,false);
    for(unsigned i=0;i<ZODIAC_COUNT;++i)button(list,settings_lang()?ZODIAC_ZH[i]:ZODIAC_EN[i],2,(int)i*48,274,choose,(void *)(uintptr_t)i);
    ui_obj_set_hidden(g_picker,true);render();refresh(NULL);
}
static void zodiac_exit(void) {
    zodiac_fetch_cancel();s_visible=s_waiting=s_ready=false;
    g_reading=g_picker=g_title=g_date=g_category=g_score=g_body=g_scroll=g_action=NULL;
}
const app_t app_zodiac={.name="Zodiac",.color=COL_TXT,.enter=zodiac_enter,.tick=zodiac_tick,.exit=zodiac_exit,
    .back=zodiac_back,.visibility=zodiac_visibility,.tick_period_ms=100};
