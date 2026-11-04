#include "app.h"
#include "settings.h"
#include "stocks_fetch.h"
#include "font_stocks.h"
#include "lvgl_compat.h"
#include "src/misc/lv_text_private.h"
#include "src/core/lv_obj_event_private.h"
#include <math.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define REFRESH_MS 15000u
#define REQUEST_WAIT_MS 30000u
#define STOCK_ROW_HEIGHT 89
#define STOCK_VIEW_TOP 122
#define STOCK_VIEW_HEIGHT (3*STOCK_ROW_HEIGHT)
#define CARD 0x0d1016
#define LINE 0x202631
#define MUTED 0x9aA3b3
#define RISE 0xff7c89
#define FALL 0x6dd7b4

typedef struct { lv_obj_t *button,*name,*price,*code,*percent,*time,*state; } stock_row_t;
static stock_list_t list;
static stock_quote_t cached[STOCK_LIST_MAX];
static bool has_quote[STOCK_LIST_MAX],visible,loaded,waiting,pending;
static stock_fetch_state_t row_states[STOCK_LIST_MAX];
static unsigned selected,delete_index,editor_market;
static uint32_t token,started,last_finished,seen_revision;
static enum { VIEW_LIST,VIEW_QUOTE,VIEW_EDITOR,VIEW_DELETE } view;
static stock_row_t rows[STOCK_LIST_MAX];
// 快照不放LVGL线程栈，避免8只行情占用其有限栈余量。/ Keep the full batch off the LVGL task's limited stack.
static stock_batch_t polled;
static lv_obj_t *root,*body,*scroller,*summary,*refresh_button,*add_control,*top_refresh,*page_label,*input,*keyboard,*editor_hint,*market_label;
static lv_obj_t *name,*symbol,*price,*change,*stamp,*source_time,*hint;
static lv_obj_t *market_buttons[4];
static const char *const market_prefixes[]={"sh","sz","hk","us"};
static const char *words(const char *zh,const char *en){return settings_lang()?zh:en;}
static void build_list(void);
static void build_quote(void);
static void begin_request(void);
static void render(void);
static void layout_rows(void);
static void add_symbol(lv_event_t *e);
static void remove_symbol(lv_event_t *e);

static lv_obj_t *panel(lv_obj_t *parent,int width,int height) {
    lv_obj_t *o=lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o,width,height);
    ui_obj_set_scrollable(o,false);
    return o;
}
static lv_obj_t *label(lv_obj_t *parent,const char *text,const lv_font_t *font,uint32_t color) {
    lv_obj_t *o=lv_label_create(parent);
    lv_label_set_text(o,text);
    lv_obj_set_style_text_font(o,font,0);
    lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    return o;
}
static lv_obj_t *button(lv_obj_t *parent,const char *text,int width,int height,lv_event_cb_t cb,void *data) {
    lv_obj_t *o=lv_button_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o,width,height);
    lv_obj_set_style_radius(o,10,0);
    lv_obj_set_style_bg_color(o,lv_color_hex(0x222831),0);
    lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_set_style_bg_color(o,lv_color_hex(0x343e4b),LV_STATE_PRESSED);
    ui_obj_set_scrollable(o,false);
    if(cb)lv_obj_add_event_cb(o,cb,LV_EVENT_CLICKED,data);
    lv_obj_t *text_obj=label(o,text,&font_stocks_16,COL_TXT);
    lv_obj_center(text_obj);
    return o;
}
static void clear_view(void) {
    lv_obj_clean(root);
    memset(rows,0,sizeof rows);
    body=scroller=summary=refresh_button=add_control=top_refresh=page_label=input=keyboard=editor_hint=market_label=NULL;
    name=symbol=price=change=stamp=source_time=hint=NULL;
}
static bool quote_view(void){return view==VIEW_LIST || view==VIEW_QUOTE;}
static void pause_refresh(void) {
    stocks_fetch_cancel();
    waiting=pending=false;
    for(unsigned i=0;i<list.count;++i)if(row_states[i]==STOCK_LOADING)row_states[i]=STOCK_FAILED;
}
static bool name_supported(const char *text) {
    uint32_t at=0;
    while(text[at]) {
        uint32_t cp=lv_text_encoded_next(text,&at);
        lv_font_glyph_dsc_t glyph;
        if(!lv_font_get_glyph_dsc(&font_stocks_22,&glyph,cp,0)||glyph.is_placeholder)return false;
    }
    return true;
}
static uint32_t quote_color(const stock_quote_t *q) {
    if(q->direction==0)return COL_TXT;
    bool us=!strncmp(q->symbol,"us",2);
    return (q->direction>0)!=us?RISE:FALL;
}
static void percent_text(char out[32],const stock_quote_t *q) {
    snprintf(out,32,"%s%s%%",q->direction>0&&q->percent[0]!='+'?"+":"",q->percent);
}
static const char *item_status(unsigned i) {
    if(row_states[i]==STOCK_LOADING)return words("更新中","Updating");
    if(row_states[i]==STOCK_READY)return words("已获取","Received");
    if(has_quote[i])return words("旧行情","Previous");
    return row_states[i]==STOCK_OFFLINE?words("离线","Offline"):words("未获取","Unavailable");
}
static void set_chip(lv_obj_t *o,const stock_quote_t *q,bool valid) {
    uint32_t ink=valid?quote_color(q):MUTED;
    uint32_t background=ink==RISE?0x2f151d:ink==FALL?0x112d25:0x252a33;
    lv_obj_set_style_text_color(o,lv_color_hex(ink),0);
    lv_obj_set_style_bg_color(o,lv_color_hex(background),0);
    lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_set_style_radius(o,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_pad_hor(o,6,0);
}
// 圆弦随实际滚动位置重算；不是把矩形卡片裁成圆形。/ Reflow along the live viewport chord, not clipped cards.
static void layout_rows(void) {
    if(!scroller)return;
    lv_obj_update_layout(root);
    lv_area_t viewport,base;
    lv_obj_get_coords(scroller,&viewport);lv_obj_get_coords(root,&base);
    unsigned first=list.count,last=0;
    float middle=(viewport.y1+viewport.y2+1)*0.5f;
    for(unsigned i=0;i<list.count;++i) {
        stock_row_t *r=&rows[i];lv_area_t a;lv_obj_get_coords(r->button,&a);
        if(a.y2<viewport.y1 || a.y1>viewport.y2)continue;
        if(first==list.count)first=i;
        last=i+1;
        float distance=(a.y1+STOCK_ROW_HEIGHT*0.5f)-middle;
        float chord=155.0f*155.0f-distance*distance;
        int inset=48+(int)lroundf(155.0f-sqrtf(chord>0?chord:0));
        if(inset>96)inset=96;
        int left=base.x1+inset-a.x1,right=base.x1+466-inset-a.x1;
        int meta=right-left-150;
        if(meta>174)meta=174;
        const lv_font_t *font=lv_obj_get_style_text_font(r->price,0);
        lv_obj_set_pos(r->name,left,5);lv_obj_set_width(r->name,meta);
        lv_obj_set_pos(r->price,right-142,31-font->line_height+font->base_line);lv_obj_set_width(r->price,142);
        lv_obj_set_pos(r->code,left,37);lv_obj_set_width(r->code,meta);
        lv_obj_set_pos(r->time,left,57);
        lv_obj_update_layout(r->percent);
        lv_obj_set_pos(r->percent,right-lv_obj_get_width(r->percent),44);
        lv_obj_update_layout(r->time);
        lv_obj_set_pos(r->state,left+lv_obj_get_width(r->time)+6,57);
        lv_obj_invalidate(r->button);
    }
    if(page_label) {
        char text[48];
        snprintf(text,sizeof text,"%02u — %02u  /  %02u",first==list.count?0:first+1,last,list.count);
        lv_label_set_text(page_label,text);
    }
}
static void scrolled(lv_event_t *e){(void)e;layout_rows();}
static void row_event(lv_event_t *e) {
    lv_event_code_t event=lv_event_get_code(e);
    // 退出后父级清理仍发送DELETE；此时页面指针已失效。/ Parent cleanup sends DELETE after exit clears page pointers.
    if(!root || (event!=LV_EVENT_HIT_TEST && event!=LV_EVENT_DRAW_MAIN))return;
    lv_obj_t *o=lv_event_get_target_obj(e);
    lv_area_t base,a;lv_obj_get_coords(root,&base);lv_obj_get_coords(o,&a);
    if(event==LV_EVENT_HIT_TEST) {
        lv_hit_test_info_t *hit=lv_event_get_hit_test_info(e);
        int dx=hit->point->x-base.x1-233,dy=hit->point->y-base.y1-233;
        hit->res=hit->res && dx*dx+dy*dy<=219*219;
        return;
    }
    if(event!=LV_EVENT_DRAW_MAIN || !scroller)return;
    lv_area_t viewport;lv_obj_get_coords(scroller,&viewport);
    int y=a.y1+86;
    if(y>=viewport.y2-14)return;
    lv_draw_line_dsc_t line;lv_draw_line_dsc_init(&line);
    line.base.obj=o;line.color=lv_color_hex(0x24272b);line.width=1;
    lv_layer_t *layer=lv_event_get_layer(e);
    int inset=60;
    for(unsigned j=0;j<12;++j) {
        float t0=j/12.0f,t1=(j+1)/12.0f;
        line.p1=(lv_point_precise_t){base.x1+inset+(466-2*inset)*t0,y-12*t0*(1-t0)};
        line.p2=(lv_point_precise_t){base.x1+inset+(466-2*inset)*t1,y-12*t1*(1-t1)};
        lv_draw_line(layer,&line);
    }
}
static void render_row(unsigned i) {
    stock_row_t *r=&rows[i];
    if(!r->button)return;
    const stock_quote_t *q=&cached[i];
    bool available=has_quote[i];
    lv_label_set_text(r->name,available&&name_supported(q->name)?q->name:list.symbols[i]);
    lv_label_set_text(r->price,available?q->price:"--");
    const lv_font_t *font=available&&strlen(q->price)>16?&font_stocks_12:available&&strlen(q->price)>12?&font_stocks_18:available&&strlen(q->price)>8?&font_stocks_24:&font_stocks_31;
    lv_obj_set_style_text_font(r->price,font,0);
    char text[48];
    snprintf(text,sizeof text,"%s / %s",list.symbols[i],available?q->currency:"---");
    for(char *at=text;*at;++at)*at=(char)toupper((unsigned char)*at);
    lv_label_set_text(r->code,text);
    if(available)percent_text(text,q);else snprintf(text,sizeof text,"--");
    lv_label_set_text(r->percent,text);
    set_chip(r->percent,q,available);
    lv_label_set_text(r->time,available?q->timestamp+5:words("尚无行情","No quote"));
    lv_label_set_text(r->state,item_status(i));
    ui_obj_set_hidden(r->state,row_states[i]==STOCK_READY);
    bool failed=row_states[i]==STOCK_FAILED || row_states[i]==STOCK_OFFLINE;
    lv_obj_set_style_text_color(r->state,lv_color_hex(failed?RISE:MUTED),0);
}
static void render_quote(void) {
    if(!name || !list.count)return;
    const stock_quote_t *q=&cached[selected];
    bool available=has_quote[selected];
    lv_label_set_text(name,available&&name_supported(q->name)?q->name:list.symbols[selected]);
    char text[80];
    snprintf(text,sizeof text,"%s  /  %s",list.symbols[selected],available?q->currency:"---");
    lv_label_set_text(symbol,text);
    lv_label_set_text(price,available?q->price:"--");
    lv_obj_set_style_text_font(price,available&&strlen(q->price)>18?&font_stocks_18:available&&strlen(q->price)>11?&font_stocks_22:&lv_font_montserrat_40,0);
    if(available) {
        char percent[32];percent_text(percent,q);
        snprintf(text,sizeof text,"%s%s   %s",q->direction>0&&q->change[0]!='+'?"+":"",q->change,percent);
        lv_label_set_text(change,text);
    }else lv_label_set_text(change,"--");
    set_chip(change,q,available);
    lv_label_set_text(stamp,available?q->timestamp:words("尚无行情","No quote yet"));
    lv_label_set_text(source_time,!strncmp(list.symbols[selected],"us",2)?words("美东时间 · 来源行情可能延迟","US Eastern / feed may be delayed"):words("沪港时间 · 来源行情可能延迟","China / HK / feed may be delayed"));
    lv_label_set_text(hint,item_status(selected));
    lv_obj_set_style_text_color(hint,lv_color_hex(row_states[selected]==STOCK_FAILED||row_states[selected]==STOCK_OFFLINE?RISE:MUTED),0);
}
static void render(void) {
    if(!quote_view())return;
    if(view==VIEW_LIST){for(unsigned i=0;i<list.count;++i)render_row(i);layout_rows();}
    else render_quote();
    if(summary && loaded) {
        unsigned ready=0,done=0;
        for(unsigned i=0;i<list.count;++i){ready+=row_states[i]==STOCK_READY;done+=row_states[i]!=STOCK_LOADING;}
        char text[80];
        if(waiting)snprintf(text,sizeof text,settings_lang()?"更新进度 %u / %u":"Updating %u / %u",done,list.count);
        else if(ready==list.count && list.count)snprintf(text,sizeof text,settings_lang()?"%02u 只自选 · 自动更新":"%02u symbols / Auto refresh",list.count);
        else if(!wifi_service_ready())snprintf(text,sizeof text,"%s",words("离线 · 保留各只股票原行情","Offline / previous quotes retained"));
        else if(list.count)snprintf(text,sizeof text,settings_lang()?"已获取 %u / %u · 部分失败":"Received %u / %u / partial failure",ready,list.count);
        else snprintf(text,sizeof text,"%s",words("添加代码，开始查看行情","Add a symbol to get started"));
        lv_label_set_text(summary,text);
    }
    if(top_refresh){if(waiting || !list.count || !loaded)lv_obj_add_state(top_refresh,LV_STATE_DISABLED);else lv_obj_remove_state(top_refresh,LV_STATE_DISABLED);}
    if(refresh_button) {
        if(waiting || !list.count || !loaded)lv_obj_add_state(refresh_button,LV_STATE_DISABLED);
        else lv_obj_remove_state(refresh_button,LV_STATE_DISABLED);
    }
}
static void begin_request(void) {
    if(!root || !visible || !quote_view() || !loaded || !list.count || waiting)return;
    seen_revision=0;
    started=lv_tick_get();
    stock_fetch_state_t state=stocks_fetch_begin(&list,&token);
    pending=state==STOCK_BUSY;
    waiting=state==STOCK_LOADING || pending;
    for(unsigned i=0;i<list.count;++i)row_states[i]=waiting?STOCK_LOADING:state;
    if(!waiting)last_finished=lv_tick_get();
    render();
}
static void retry(lv_event_t *e){(void)e;begin_request();}
static void show_list(lv_event_t *e){(void)e;build_list();}
static void choose(lv_event_t *e){selected=(unsigned)(uintptr_t)lv_event_get_user_data(e);build_quote();}
// 弧形控件沿显示圆心绘制，并按环带命中；矩形边角不能误触。/ Draw and hit-test the same annular sector.
static void arc_control_event(lv_event_t *e) {
    lv_obj_t *o=lv_event_get_target_obj(e);lv_area_t a;lv_obj_get_coords(o,&a);
    bool right=(uintptr_t)lv_event_get_user_data(e)!=0;
    float cx=a.x1+233-(right?238:135),cy=a.y1+233-386;
    int start=right?62:92,end=right?88:118;
    if(lv_event_get_code(e)==LV_EVENT_HIT_TEST) {
        lv_hit_test_info_t *hit=lv_event_get_hit_test_info(e);
        float dx=hit->point->x-cx,dy=hit->point->y-cy,radius=sqrtf(dx*dx+dy*dy);
        float angle=atan2f(dy,dx)*180.0f/3.14159265f;
        if(angle<0)angle+=360;
        hit->res=hit->res && radius>=167 && radius<=215 && angle>=start-1 && angle<=end+1;
        return;
    }
    if(lv_event_get_code(e)!=LV_EVENT_DRAW_MAIN)return;
    lv_draw_arc_dsc_t arc;lv_draw_arc_dsc_init(&arc);
    arc.base.obj=o;arc.center=(lv_point_t){(int32_t)lroundf(cx),(int32_t)lroundf(cy)};
    arc.radius=207;arc.width=32;arc.start_angle=start;arc.end_angle=end;
    arc.color=lv_color_hex(lv_obj_has_state(o,LV_STATE_PRESSED)?0x25282e:0x17191c);
    arc.opa=lv_obj_has_state(o,LV_STATE_DISABLED)?LV_OPA_40:LV_OPA_COVER;
    lv_layer_t *layer=lv_event_get_layer(e);lv_draw_arc(layer,&arc);
    arc.color=lv_color_hex(0x34373b);arc.width=1;lv_draw_arc(layer,&arc);arc.radius=176;lv_draw_arc(layer,&arc);
    lv_draw_line_dsc_t line;lv_draw_line_dsc_init(&line);
    line.base.obj=o;line.color=lv_color_hex(0x34373b);line.width=1;line.opa=arc.opa;
    for(int angle=start;angle<=end;angle+=end-start) {
        float radians=angle*3.14159265f/180;
        line.p1=(lv_point_precise_t){cx+175*cosf(radians),cy+175*sinf(radians)};
        line.p2=(lv_point_precise_t){cx+207*cosf(radians),cy+207*sinf(radians)};
        lv_draw_line(layer,&line);
    }
}
static lv_obj_t *arc_button(const char *text,bool right,lv_event_cb_t cb) {
    int x=right?238:135;
    lv_obj_t *o=lv_button_create(root);lv_obj_remove_style_all(o);
    lv_obj_set_pos(o,x,386);lv_obj_set_size(o,94,56);ui_obj_set_scrollable(o,false);
    lv_obj_add_flag(o,LV_OBJ_FLAG_ADV_HITTEST);lv_obj_set_ext_click_area(o,8);
    lv_obj_add_event_cb(o,arc_control_event,LV_EVENT_ALL,(void *)(uintptr_t)right);
    lv_obj_add_event_cb(o,cb,LV_EVENT_CLICKED,NULL);
    lv_obj_set_style_opa(o,LV_OPA_50,LV_STATE_DISABLED);
    lv_obj_t *caption=label(o,text,&font_stocks_14,COL_TXT);lv_obj_update_layout(caption);
    lv_obj_set_pos(caption,(right?283:183)-x-lv_obj_get_width(caption)/2,22);
    return o;
}
static void refresh_icon(lv_event_t *e) {
    if(lv_event_get_code(e)!=LV_EVENT_DRAW_MAIN)return;
    lv_obj_t *o=lv_event_get_target_obj(e);lv_area_t a;lv_obj_get_coords(o,&a);
    lv_draw_arc_dsc_t arc;lv_draw_arc_dsc_init(&arc);
    arc.base.obj=o;arc.center=(lv_point_t){a.x1+22,a.y1+22};
    arc.radius=10;arc.width=2;arc.start_angle=30;arc.end_angle=310;arc.rounded=true;arc.color=lv_color_hex(0xc5c7c4);
    arc.opa=lv_obj_has_state(o,LV_STATE_DISABLED)?LV_OPA_40:LV_OPA_COVER;
    lv_draw_arc(lv_event_get_layer(e),&arc);
    lv_draw_line_dsc_t line;lv_draw_line_dsc_init(&line);
    line.base.obj=o;line.width=2;line.color=arc.color;line.opa=arc.opa;line.round_start=line.round_end=true;
    line.p1=(lv_point_precise_t){a.x1+27,a.y1+9};line.p2=(lv_point_precise_t){a.x1+27,a.y1+17};lv_draw_line(lv_event_get_layer(e),&line);
    line.p1=line.p2;line.p2=(lv_point_precise_t){a.x1+19,a.y1+17};lv_draw_line(lv_event_get_layer(e),&line);
}
static void footer(void) {
    summary=label(root,"",&font_stocks_12,0x8d9095);
    lv_obj_set_width(summary,288);lv_obj_set_style_text_align(summary,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_align(summary,LV_ALIGN_TOP_MID,0,92);
    top_refresh=lv_button_create(root);lv_obj_remove_style_all(top_refresh);
    lv_obj_set_pos(top_refresh,319,53);lv_obj_set_size(top_refresh,44,44);ui_obj_set_scrollable(top_refresh,false);
    lv_obj_add_event_cb(top_refresh,refresh_icon,LV_EVENT_DRAW_MAIN,NULL);lv_obj_add_event_cb(top_refresh,retry,LV_EVENT_CLICKED,NULL);
    page_label=label(root,"",&font_stocks_12,0x797d82);
    lv_obj_set_width(page_label,210);lv_obj_set_style_text_align(page_label,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_align(page_label,LV_ALIGN_TOP_MID,0,377);
    add_control=arc_button(view==VIEW_QUOTE?words("自选","List"):words("＋ 添加","+ Add"),false,view==VIEW_QUOTE?show_list:add_symbol);
    if(view==VIEW_LIST && (!loaded || list.count>=STOCK_LIST_MAX))lv_obj_add_state(add_control,LV_STATE_DISABLED);
    refresh_button=arc_button(words("刷新","Refresh"),true,retry);
}
static void reload(lv_event_t *e) {
    (void)e;
    stock_list_t read={0};
    loaded=stock_list_load(&read);
    if(loaded){list=read;memset(has_quote,0,sizeof has_quote);}
    build_list();
    begin_request();
}
static void build_list(void) {
    clear_view();
    view=VIEW_LIST;
    if(!loaded || !list.count) {
        lv_obj_t *empty=label(root,!loaded?words("自选读取失败","Unable to read watchlist"):words("从第一只股票开始","Your market, at a glance"),&font_stocks_24,COL_TXT);
        lv_obj_set_width(empty,306);lv_obj_set_style_text_align(empty,LV_TEXT_ALIGN_CENTER,0);
        lv_obj_align(empty,LV_ALIGN_TOP_MID,0,194);
        lv_obj_t *action=button(root,!loaded?words("重试读取","Retry"):words("添加自选","Add your first symbol"),260,44,!loaded?reload:add_symbol,NULL);
        lv_obj_align(action,LV_ALIGN_TOP_MID,0,254);
        footer();render();return;
    }
    scroller=panel(root,426,STOCK_VIEW_HEIGHT);
    lv_obj_align(scroller,LV_ALIGN_TOP_MID,0,STOCK_VIEW_TOP);
    lv_obj_set_scroll_dir(scroller,LV_DIR_VER);lv_obj_set_scrollbar_mode(scroller,LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_snap_y(scroller,LV_SCROLL_SNAP_START);
    lv_obj_set_flex_flow(scroller,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_row(scroller,0,0);
    ui_obj_set_scroll_elastic(scroller,false);
    lv_obj_add_event_cb(scroller,scrolled,LV_EVENT_SCROLL,NULL);
    for(unsigned i=0;i<list.count;++i) {
        stock_row_t *r=&rows[i];
        r->button=lv_button_create(scroller);lv_obj_remove_style_all(r->button);
        lv_obj_set_size(r->button,426,STOCK_ROW_HEIGHT);ui_obj_set_scrollable(r->button,false);
        lv_obj_add_flag(r->button,LV_OBJ_FLAG_SNAPPABLE|LV_OBJ_FLAG_ADV_HITTEST);
        lv_obj_add_event_cb(r->button,choose,LV_EVENT_CLICKED,(void *)(uintptr_t)i);
        lv_obj_add_event_cb(r->button,row_event,LV_EVENT_ALL,(void *)(uintptr_t)i);
        r->name=label(r->button,"",&font_stocks_22,COL_TXT);lv_obj_set_height(r->name,28);lv_label_set_long_mode(r->name,LV_LABEL_LONG_MODE_DOTS);
        r->price=label(r->button,"--",&font_stocks_31,COL_TXT);lv_obj_set_style_text_align(r->price,LV_TEXT_ALIGN_RIGHT,0);
        lv_label_set_long_mode(r->price,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        r->code=label(r->button,"",&font_stocks_12,0x93989e);lv_label_set_long_mode(r->code,LV_LABEL_LONG_MODE_DOTS);
        r->percent=label(r->button,"--",&font_stocks_17,MUTED);lv_obj_set_style_min_width(r->percent,78,0);lv_obj_set_height(r->percent,27);
        lv_obj_set_style_text_align(r->percent,LV_TEXT_ALIGN_CENTER,0);
        r->time=label(r->button,"",&font_stocks_12,0x858a90);
        r->state=label(r->button,"",&font_stocks_12,MUTED);
    }
    footer();render();
}
static void build_quote(void) {
    clear_view();
    view=VIEW_QUOTE;
    body=panel(root,320,276);
    lv_obj_align(body,LV_ALIGN_TOP_MID,0,102);
    lv_obj_set_style_bg_color(body,lv_color_hex(CARD),0);
    lv_obj_set_style_bg_opa(body,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(body,1,0);
    lv_obj_set_style_border_color(body,lv_color_hex(LINE),0);
    lv_obj_set_style_radius(body,22,0);
    lv_obj_set_style_pad_all(body,16,0);
    lv_obj_set_flex_flow(body,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body,5,0);
    lv_obj_t *top=panel(body,286,28);
    name=label(top,"",&font_stocks_20,COL_TXT);
    lv_obj_set_width(name,206);lv_label_set_long_mode(name,LV_LABEL_LONG_MODE_DOTS);
    lv_obj_t *remove=button(top,words("删除","Remove"),66,28,remove_symbol,(void *)(uintptr_t)selected);
    lv_obj_align(remove,LV_ALIGN_RIGHT_MID,0,0);
    symbol=label(body,"",&font_stocks_14,MUTED);
    price=label(body,"--",&lv_font_montserrat_40,COL_TXT);lv_obj_set_height(price,52);
    change=label(body,"",&font_stocks_18,COL_TXT);
    stamp=label(body,"",&font_stocks_16,COL_TXT);
    source_time=label(body,"",&font_stocks_14,MUTED);lv_obj_set_width(source_time,286);
    hint=label(body,"",&font_stocks_14,MUTED);
    footer();
    render();
}
static void back_list(lv_event_t *e){(void)e;build_list();begin_request();}
static void confirm_delete(lv_event_t *e) {
    (void)e;
    stock_list_t next=list;
    for(unsigned i=delete_index+1;i<next.count;++i)memcpy(next.symbols[i-1],next.symbols[i],STOCK_SYMBOL_BYTES);
    --next.count;
    memset(next.symbols[next.count],0,STOCK_SYMBOL_BYTES);
    if(!stock_list_save(&next)){lv_label_set_text(editor_hint,words("保存失败，请重试","Save failed, retry"));return;}
    for(unsigned i=delete_index+1;i<list.count;++i){cached[i-1]=cached[i];has_quote[i-1]=has_quote[i];row_states[i-1]=row_states[i];}
    list=next;
    has_quote[list.count]=false;
    selected=0;
    build_list();
    begin_request();
}
static void remove_symbol(lv_event_t *e) {
    delete_index=(unsigned)(uintptr_t)lv_event_get_user_data(e);
    pause_refresh();
    clear_view();
    view=VIEW_DELETE;
    lv_obj_t *title=label(root,words("移出自选列表？","Remove this symbol?"),&font_stocks_24,COL_TXT);
    lv_obj_align(title,LV_ALIGN_TOP_MID,0,150);
    lv_obj_t *code=label(root,list.symbols[delete_index],&font_stocks_20,MUTED);
    lv_obj_align(code,LV_ALIGN_TOP_MID,0,196);
    editor_hint=label(root,"",&font_stocks_14,RISE);
    lv_obj_align(editor_hint,LV_ALIGN_TOP_MID,0,232);
    lv_obj_t *confirm=button(root,words("确认移出","Remove symbol"),242,40,confirm_delete,NULL);
    lv_obj_align(confirm,LV_ALIGN_TOP_MID,0,272);
    lv_obj_set_style_bg_color(confirm,lv_color_hex(0x351d25),0);
    lv_obj_t *cancel=button(root,words("保留自选","Keep symbol"),242,40,back_list,NULL);
    lv_obj_align(cancel,LV_ALIGN_TOP_MID,0,322);
}
static void save_symbol(lv_event_t *e) {
    (void)e;
    char full[32],normalized[STOCK_SYMBOL_BYTES];
    snprintf(full,sizeof full,"%s%s",market_prefixes[editor_market],lv_textarea_get_text(input));
    if(!stock_symbol_normalize(full,normalized)){lv_label_set_text(editor_hint,words("请检查股票代码","Check the symbol"));return;}
    for(unsigned i=0;i<list.count;++i)if(!strcmp(normalized,list.symbols[i])){lv_label_set_text(editor_hint,words("已经在自选中","Already in watchlist"));return;}
    if(list.count>=STOCK_LIST_MAX){lv_label_set_text(editor_hint,words("最多添加8只","Up to 8 symbols"));return;}
    stock_list_t next=list;
    memcpy(next.symbols[next.count++],normalized,sizeof normalized);
    if(!stock_list_save(&next)){lv_label_set_text(editor_hint,words("保存失败，请重试","Save failed, retry"));return;}
    has_quote[list.count]=false;
    list=next;
    build_list();
    begin_request();
}
static void keyboard_event(lv_event_t *e) {
    if(lv_event_get_code(e)==LV_EVENT_READY)save_symbol(e);
    else if(lv_event_get_code(e)==LV_EVENT_CANCEL)back_list(e);
}
static void configure_market(void) {
    bool us=editor_market==3,hk=editor_market==2;
    const char *zh[]={"沪股代码","深股代码","港股代码","美股代码"};
    const char *en[]={"Shanghai symbol","Shenzhen symbol","Hong Kong symbol","US ticker"};
    lv_label_set_text(market_label,settings_lang()?zh[editor_market]:en[editor_market]);
    lv_textarea_set_text(input,"");
    lv_textarea_set_accepted_chars(input,us?"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ":"0123456789");
    lv_textarea_set_max_length(input,us?10:hk?5:6);
    lv_textarea_set_placeholder_text(input,us?"AAPL":hk?"00700":editor_market==1?"000001":"600519");
    lv_keyboard_set_mode(keyboard,us?LV_KEYBOARD_MODE_TEXT_UPPER:LV_KEYBOARD_MODE_NUMBER);
    for(unsigned i=0;i<4;++i)lv_obj_set_style_bg_color(market_buttons[i],lv_color_hex(i==editor_market?0x55303a:0x222831),0);
    lv_label_set_text(editor_hint,"");
}
static void select_market(lv_event_t *e){editor_market=(unsigned)(uintptr_t)lv_event_get_user_data(e);configure_market();}
static void add_symbol(lv_event_t *e) {
    (void)e;
    pause_refresh();
    clear_view();
    view=VIEW_EDITOR;
    editor_market=0;
    const char *zh[]={"沪股","深股","港股","美股"},*en[]={"SH","SZ","HK","US"};
    lv_obj_t *tabs=panel(root,266,32);lv_obj_align(tabs,LV_ALIGN_TOP_MID,0,126);
    lv_obj_set_flex_flow(tabs,LV_FLEX_FLOW_ROW);lv_obj_set_style_pad_column(tabs,6,0);
    for(unsigned i=0;i<4;++i)market_buttons[i]=button(tabs,settings_lang()?zh[i]:en[i],62,32,select_market,(void *)(uintptr_t)i);
    market_label=label(root,"",&font_stocks_16,MUTED);lv_obj_align(market_label,LV_ALIGN_TOP_MID,0,168);
    input=lv_textarea_create(root);lv_obj_set_size(input,288,48);lv_obj_align(input,LV_ALIGN_TOP_MID,0,198);
    lv_textarea_set_one_line(input,true);
    lv_obj_set_style_text_font(input,&font_stocks_20,0);
    lv_obj_set_style_text_color(input,lv_color_hex(COL_TXT),0);
    lv_obj_set_style_bg_color(input,lv_color_hex(CARD),0);
    lv_obj_set_style_border_color(input,lv_color_hex(LINE),0);
    lv_obj_set_style_border_color(input,lv_color_hex(COL_RED),LV_PART_CURSOR);
    editor_hint=label(root,"",&font_stocks_14,RISE);lv_obj_set_width(editor_hint,280);
    lv_obj_set_style_text_align(editor_hint,LV_TEXT_ALIGN_CENTER,0);lv_obj_align(editor_hint,LV_ALIGN_TOP_MID,0,242);
    keyboard=lv_keyboard_create(root);lv_obj_set_size(keyboard,316,128);lv_obj_align(keyboard,LV_ALIGN_TOP_MID,0,266);
    lv_keyboard_set_textarea(keyboard,input);
    lv_obj_set_style_text_font(keyboard,&lv_font_montserrat_20,LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard,lv_color_hex(COL_BG),0);
    lv_obj_set_style_bg_color(keyboard,lv_color_hex(0x252d38),LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard,lv_color_hex(COL_TXT),LV_PART_ITEMS);
    lv_obj_set_style_border_width(keyboard,0,LV_PART_ITEMS);
    lv_obj_add_event_cb(keyboard,keyboard_event,LV_EVENT_ALL,NULL);
    configure_market();
}
static void enter(lv_obj_t *parent) {
    visible=true;
    selected=0;
    stock_list_t read={0};
    loaded=stock_list_load(&read);
    list=loaded?read:(stock_list_t){0};
    memset(has_quote,0,sizeof has_quote);
    for(unsigned i=0;i<list.count;++i)row_states[i]=STOCK_LOADING;
    root=panel(parent,466,466);
    launcher_set_title(words("自选行情","Watchlist"));
    build_list();
    begin_request();
}
static void fail_pending(stock_fetch_state_t state) {
    for(unsigned i=0;i<list.count;++i)if(row_states[i]==STOCK_LOADING)row_states[i]=state;
    waiting=pending=false;
    last_finished=lv_tick_get();
    render();
}
static void tick(void) {
    if(!root || !visible || !quote_view() || !loaded || !list.count)return;
    uint32_t now=lv_tick_get();
    if(pending) {
        stock_fetch_state_t state=stocks_fetch_begin(&list,&token);
        if(state==STOCK_BUSY && now-started<10000)return;
        pending=false;
        if(state!=STOCK_LOADING){fail_pending(state==STOCK_OFFLINE?state:STOCK_FAILED);return;}
        started=now;
    }
    if(waiting) {
        if(!stocks_fetch_poll(token,&polled)){fail_pending(STOCK_FAILED);return;}
        if(polled.revision!=seen_revision) {
            seen_revision=polled.revision;
            // 用同一轮的代码身份核对每个位置，不能把旧列表的结果拼进新列表。
            // Match every slot to this round's symbol before merging partial results.
            if(polled.list.count!=list.count){pause_refresh();fail_pending(STOCK_FAILED);return;}
            for(unsigned i=0;i<list.count;++i) {
                if(strcmp(polled.list.symbols[i],list.symbols[i])){pause_refresh();fail_pending(STOCK_FAILED);return;}
            }
            for(unsigned i=0;i<list.count;++i) {
                row_states[i]=polled.states[i];
                if(polled.states[i]==STOCK_READY && !strcmp(polled.quotes[i].symbol,list.symbols[i])) {
                    cached[i]=polled.quotes[i];has_quote[i]=true;
                }else if(polled.states[i]==STOCK_READY)row_states[i]=STOCK_FAILED;
            }
            if(polled.complete){waiting=false;last_finished=now;}
            render();
        }
        if(waiting && now-started>=REQUEST_WAIT_MS){stocks_fetch_cancel();fail_pending(STOCK_FAILED);}
    }else {
        bool was_offline=false;
        for(unsigned i=0;i<list.count;++i)was_offline|=row_states[i]==STOCK_OFFLINE;
        if(now-last_finished>=REFRESH_MS || (was_offline&&wifi_service_ready()))begin_request();
    }
}
static bool back(void) {
    if(view==VIEW_LIST)return false;
    bool paused=!quote_view();
    build_list();
    if(paused)begin_request();
    return true;
}
static void visibility(bool value) {
    visible=value;
    if(!value)pause_refresh();
    else if(quote_view())begin_request();
}
static void exit_app(void) {
    pause_refresh();
    visible=false;
    root=body=scroller=summary=refresh_button=add_control=top_refresh=page_label=input=keyboard=editor_hint=market_label=NULL;
    name=symbol=price=change=stamp=source_time=hint=NULL;
    memset(rows,0,sizeof rows);
    memset(market_buttons,0,sizeof market_buttons);
}
const app_t app_stocks={.name="Stocks",.color=COL_TXT,.enter=enter,.tick=tick,.exit=exit_app,.back=back,.tick_period_ms=100,.visibility=visibility};
