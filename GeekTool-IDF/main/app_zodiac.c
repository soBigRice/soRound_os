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
static lv_obj_t *g_emblem,*g_rating,*g_tabs[5],*g_choices[12];
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
static void line(lv_layer_t *layer,lv_obj_t *o,float x,float y,float x2,float y2,int width,uint32_t color) {
    lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);d.base.obj=o;d.width=width;d.color=lv_color_hex(color);
    d.round_start=d.round_end=true;d.p1=(lv_point_precise_t){x,y};d.p2=(lv_point_precise_t){x2,y2};
    lv_draw_line(layer,&d);
}
static void curve(lv_layer_t *l,lv_obj_t *o,int x,int y,const float *p) {
    float px=p[0],py=p[1];
    for(int i=1;i<=16;++i) {
        float t=i/16.0f,u=1-t;
        float nx=u*u*u*p[0]+3*u*u*t*p[2]+3*u*t*t*p[4]+t*t*t*p[6];
        float ny=u*u*u*p[1]+3*u*u*t*p[3]+3*u*t*t*p[5]+t*t*t*p[7];
        line(l,o,x+px,y+py,x+nx,y+ny,3,0xf0e6d7);px=nx;py=ny;
    }
}
static void arc(lv_layer_t *l,lv_obj_t *o,int x,int y,int radius,int start,int end,int width,uint32_t color) {
    lv_draw_arc_dsc_t d;lv_draw_arc_dsc_init(&d);d.base.obj=o;d.center=(lv_point_t){x,y};d.radius=radius;
    d.start_angle=start;d.end_angle=end;d.width=width;d.rounded=true;d.color=lv_color_hex(color);lv_draw_arc(l,&d);
}
static void emblem_draw(lv_event_t *e) {
    lv_obj_t *o=lv_event_get_target_obj(e);lv_layer_t *l=lv_event_get_layer(e);lv_area_t a;lv_obj_get_coords(o,&a);
    int x=a.x1,y=a.y1;unsigned sign=(unsigned)(uintptr_t)lv_event_get_user_data(e);
    arc(l,o,x+32,y+32,30,0,360,1,0x49413a);
    arc(l,o,x+53,y+11,3,0,360,3,COL_RED);
#define C(...) curve(l,o,x,y,(const float[]){__VA_ARGS__})
#define L(x1,y1,x2,y2) line(l,o,x+(x1),y+(y1),x+(x2),y+(y2),3,0xf0e6d7)
#define A(cx,cy,r,s,e) arc(l,o,x+(cx),y+(cy),r,s,e,3,0xf0e6d7)
    // Twelve conventional zodiac symbols, drawn locally without emoji/font fallback.
    switch(sign) {
        case 0:C(16,28,7,8,34,7,32,47);C(48,28,57,8,30,7,32,47);break;
        case 1:A(32,37,12,0,360);C(16,15,18,33,46,33,48,15);break;
        case 2:L(23,17,23,47);L(41,17,41,47);C(15,14,25,20,39,20,49,14);C(15,50,25,44,39,44,49,50);break;
        case 3:A(20,26,6,0,360);A(44,39,6,0,360);C(14,25,14,7,40,10,49,22);C(50,40,50,58,24,55,15,43);break;
        case 4:A(20,38,7,0,360);C(26,36,48,3,14,3,30,27);C(30,27,57,55,47,57,40,43);break;
        case 5:L(16,47,16,21);C(16,27,17,10,29,10,29,26);L(29,26,29,47);C(29,27,29,10,42,10,42,26);C(42,26,60,5,51,49,36,50);break;
        case 6:L(14,47,50,47);L(14,35,22,35);L(42,35,50,35);A(32,31,10,180,360);L(22,31,22,35);L(42,31,42,35);break;
        case 7:L(15,46,15,22);C(15,26,16,11,28,11,28,25);L(28,25,28,46);C(28,26,28,11,41,11,41,25);C(41,25,41,52,47,52,52,43);L(52,43,46,44);L(52,43,52,49);break;
        case 8:L(17,48,47,17);L(47,17,31,17);L(47,17,47,33);L(20,27,37,44);break;
        case 9:C(13,22,25,2,26,60,34,28);C(34,28,43,7,55,24,40,36);C(40,36,17,61,50,64,48,41);break;
        case 10:for(int r=0;r<2;++r){int v=23+r*17;L(13,v+5,22,v-4);L(22,v-4,32,v+5);L(32,v+5,42,v-4);L(42,v-4,51,v+5);}break;
        case 11:C(18,14,30,23,30,41,18,50);C(46,14,34,23,34,41,46,50);L(16,32,48,32);break;
    }
#undef C
#undef L
#undef A
}
static void rating_draw(lv_event_t *e) {
    lv_obj_t *o=lv_event_get_target_obj(e);lv_layer_t *l=lv_event_get_layer(e);lv_area_t a;lv_obj_get_coords(o,&a);
    unsigned score=s_ready?s_data.score[s_category]:UINT8_MAX;
    for(unsigned i=0;i<5;++i)arc(l,o,a.x1+5+(int)i*16,a.y1+8,4,0,360,4,i<score&&score<=5?0xe5c38d:0x343438);
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
    for(unsigned i=0;i<5;++i) {
        bool selected=i==s_category;
        lv_obj_set_style_bg_color(g_tabs[i],lv_color_hex(selected?0x52252c:0x171719),0);
        lv_obj_set_style_text_color(lv_obj_get_child(g_tabs[i],0),lv_color_hex(selected?COL_TXT:0xaaa6a1),0);
    }
    lv_obj_invalidate(g_rating);
    char rating[32];uint8_t n=s_data.score[s_category];
    snprintf(rating,sizeof rating,n<=5?"%u/5":"--/5",n<=5?n:0);
    lv_label_set_text(g_score,s_ready?rating:"");
    if(!s_ready)return;
    char date[96];snprintf(date,sizeof date,settings_lang()?"%s · 今日":"%s · CN",s_data.date);
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
    lv_label_set_text(g_date,words("DAILY · CN ORIGINAL","每日更新 · 联网获取"));
    lv_label_set_text(g_score,"");
    lv_obj_invalidate(g_rating);
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
    lv_obj_remove_event_cb(g_emblem,emblem_draw);
    lv_obj_add_event_cb(g_emblem,emblem_draw,LV_EVENT_DRAW_MAIN,(void *)(uintptr_t)s_sign);
    lv_obj_invalidate(g_emblem);
    ui_obj_set_hidden(g_picker,true);ui_obj_set_hidden(g_reading,false);render();refresh(NULL);
}
static void picker(lv_event_t *e) {
    (void)e;s_selecting=true;ui_obj_set_hidden(g_reading,true);ui_obj_set_hidden(g_picker,false);
    for(unsigned i=0;i<ZODIAC_COUNT;++i)lv_obj_set_style_bg_color(g_choices[i],lv_color_hex(i==s_sign?0x52252c:0x171719),0);
}
static void category(lv_event_t *e) {
    s_category=(unsigned)(uintptr_t)lv_event_get_user_data(e);render();
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
    g_reading=block(parent,0,96,466,356);
    g_emblem=block(g_reading,86,2,64,64);lv_obj_add_event_cb(g_emblem,emblem_draw,LV_EVENT_DRAW_MAIN,(void *)(uintptr_t)s_sign);
    ui_obj_set_clickable(g_emblem,true);lv_obj_add_event_cb(g_emblem,picker,LV_EVENT_CLICKED,NULL);
    g_title=label(g_reading,settings_lang()?ZODIAC_ZH[s_sign]:ZODIAC_EN[s_sign],169,7,148,&font_location_24,COL_TXT);
    lv_obj_set_style_text_align(g_title,LV_TEXT_ALIGN_LEFT,0);
    ui_obj_set_clickable(g_title,true);lv_obj_add_event_cb(g_title,picker,LV_EVENT_CLICKED,NULL);
    button(g_reading,words("EDIT","切换"),322,4,58,picker,NULL);
    g_date=label(g_reading,"",169,39,220,&font_location_24,0x9e9993);
    lv_obj_set_style_text_align(g_date,LV_TEXT_ALIGN_LEFT,0);
    static const char *en[]={"All","Love","Work","Cash","Body"};
    static const char *zh[]={"综合","爱情","事业","财富","健康"};
    for(unsigned i=0;i<5;++i)g_tabs[i]=button(g_reading,settings_lang()?zh[i]:en[i],56+(int)i*72,78,66,category,(void *)(uintptr_t)i);
    g_category=label(g_reading,"",86,126,160,&font_location_24,COL_TXT);
    lv_obj_set_style_text_align(g_category,LV_TEXT_ALIGN_LEFT,0);
    g_rating=block(g_reading,248,133,76,16);lv_obj_add_event_cb(g_rating,rating_draw,LV_EVENT_DRAW_MAIN,NULL);
    g_score=label(g_reading,"",326,126,62,&font_location_24,0xe5c38d);
    lv_obj_t *divider=block(g_reading,86,160,294,1);lv_obj_set_style_bg_color(divider,lv_color_hex(0x303033),0);lv_obj_set_style_bg_opa(divider,LV_OPA_COVER,0);
    g_scroll=block(g_reading,86,170,294,127);ui_obj_set_scrollable(g_scroll,true);
    lv_obj_set_style_bg_color(g_scroll,lv_color_hex(COL_TXT),LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(g_scroll,LV_OPA_50,LV_PART_SCROLLBAR);lv_obj_set_style_width(g_scroll,3,LV_PART_SCROLLBAR);
    lv_obj_set_scroll_dir(g_scroll,LV_DIR_VER);lv_obj_set_scrollbar_mode(g_scroll,LV_SCROLLBAR_MODE_AUTO);
    ui_obj_set_scroll_elastic(g_scroll,false);ui_obj_set_scroll_chain_ver(g_scroll,false);
    g_body=label(g_scroll,"",0,0,282,&font_answer_32,0xe8e5df);
    lv_obj_set_style_text_align(g_body,LV_TEXT_ALIGN_LEFT,0);lv_obj_set_style_text_line_space(g_body,7,0);
    g_action=button(g_reading,words("REFRESH","刷新运势"),147,309,172,refresh,NULL);
    lv_obj_set_style_bg_opa(g_action,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(g_action,1,0);
    lv_obj_set_style_border_color(g_action,lv_color_hex(0x49413a),0);
    g_picker=block(parent,83,107,300,291);
    label(g_picker,words("CHOOSE YOUR SIGN","选择你的星座"),0,0,300,&font_location_24,COL_TXT);
    lv_obj_t *list=block(g_picker,7,43,286,238);ui_obj_set_scrollable(list,true);
    lv_obj_set_scroll_dir(list,LV_DIR_VER);lv_obj_set_scrollbar_mode(list,LV_SCROLLBAR_MODE_AUTO);
    ui_obj_set_scroll_elastic(list,false);ui_obj_set_scroll_chain_ver(list,false);
    for(unsigned i=0;i<ZODIAC_COUNT;++i)g_choices[i]=button(list,settings_lang()?ZODIAC_ZH[i]:ZODIAC_EN[i],2,(int)i*48,274,choose,(void *)(uintptr_t)i);
    ui_obj_set_hidden(g_picker,true);render();refresh(NULL);
}
static void zodiac_exit(void) {
    zodiac_fetch_cancel();s_visible=s_waiting=s_ready=false;
    g_reading=g_picker=g_title=g_date=g_category=g_score=g_body=g_scroll=g_action=NULL;
    g_emblem=g_rating=NULL;memset(g_tabs,0,sizeof g_tabs);memset(g_choices,0,sizeof g_choices);
}
const app_t app_zodiac={.name="Zodiac",.color=COL_TXT,.enter=zodiac_enter,.tick=zodiac_tick,.exit=zodiac_exit,
    .back=zodiac_back,.visibility=zodiac_visibility,.tick_period_ms=100};
