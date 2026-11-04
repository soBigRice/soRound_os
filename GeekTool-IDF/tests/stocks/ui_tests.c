#include "../lifestyle/native_ui.h"
#include "fixture.h"
#include "stocks_fetch.h"
#include "src/misc/lv_area_private.h"

static stock_list_t stored;
static bool read_ok=true,save_ok=true,online=true,busy;
static unsigned saves,requests,cancels;
static uint32_t mock_token;
static stock_batch_t mock_batch;
static lv_obj_t *stock_test_title;
void launcher_set_title(const char *text){assert(stock_test_title);lv_label_set_text(stock_test_title,text);}
static lv_indev_t *pointer;
static lv_point_t pointer_point;
static lv_indev_state_t pointer_state;
static void pointer_read(lv_indev_t *device,lv_indev_data_t *data){(void)device;data->point=pointer_point;data->state=pointer_state;}
static void tap_point(int x,int y){pointer_point=(lv_point_t){x,y};pointer_state=LV_INDEV_STATE_PRESSED;lv_indev_read(pointer);pointer_state=LV_INDEV_STATE_RELEASED;lv_indev_read(pointer);}
static const char *const symbols[]={"sh600519","sz000001","hk00700","usAAPL","hk80700","sh000001","sz399001","sh601318"};
bool stock_list_load(stock_list_t *out){if(!read_ok)return false;*out=stored;return true;}
bool stock_list_save(const stock_list_t *next){++saves;if(!save_ok)return false;stored=*next;return true;}
bool wifi_service_ready(void){return online;}
stock_fetch_state_t stocks_fetch_begin(const stock_list_t *next,uint32_t *out) {
    if(!online)return STOCK_OFFLINE;
    if(busy)return STOCK_BUSY;
    ++requests;*out=++mock_token;
    mock_batch=(stock_batch_t){.list=*next,.revision=1};
    for(unsigned i=0;i<next->count;++i)mock_batch.states[i]=STOCK_LOADING;
    return STOCK_LOADING;
}
bool stocks_fetch_poll(uint32_t ticket,stock_batch_t *out){if(ticket!=mock_token||!out)return false;*out=mock_batch;return true;}
void stocks_fetch_cancel(void){++cancels;++mock_token;}
#include "../../main/app_stocks.c"

static void result(unsigned i,stock_fetch_state_t state) {
    assert(i<mock_batch.list.count);
    if(state==STOCK_READY) {
        char json[STOCK_HTTP_BYTES];size_t bytes=stock_fixture(mock_batch.list.symbols[i],json);
        assert(stock_quote_parse(json,bytes,mock_batch.list.symbols[i],&mock_batch.quotes[i]));
    }
    mock_batch.states[i]=state;++mock_batch.revision;
}
static void finish(void) {
    for(unsigned i=0;i<mock_batch.list.count;++i)if(mock_batch.states[i]==STOCK_LOADING)result(i,STOCK_READY);
    mock_batch.complete=true;++mock_batch.revision;app_stocks.tick();
}
static lv_obj_t *find_text(lv_obj_t *o,const char *text) {
    if(lv_obj_check_type(o,&lv_label_class)&&!strcmp(lv_label_get_text(o),text))return o;
    for(unsigned i=0;i<lv_obj_get_child_count(o);++i){lv_obj_t *found=find_text(lv_obj_get_child(o,i),text);if(found)return found;}
    return NULL;
}
static void click_text(const char *text){lv_obj_t *o=find_text(root,text);assert(o);tap(lv_obj_get_parent(o));}
static void visible_controls(lv_obj_t *o,lv_area_t clip) {
    if(ui_obj_is_hidden(o))return;
    lv_area_t bounds,area;lv_obj_get_coords(o,&bounds);
    if(!lv_area_intersect(&area,&bounds,&clip))return;
    if(scroller && lv_obj_get_parent(o)==scroller) {
        for(unsigned i=0;i<4;++i){lv_point_t point={i&1?area.x2:area.x1,i&2?area.y2:area.y1};if(hypot(point.x-233,point.y-233)>219)assert(!lv_obj_hit_test(o,&point));}
    }else if(lv_obj_check_type(o,&lv_button_class)||lv_obj_check_type(o,&lv_textarea_class)||lv_obj_check_type(o,&lv_keyboard_class)) {
        for(unsigned i=0;i<4;++i){int x=i&1?area.x2:area.x1,y=i&2?area.y2:area.y1;assert(hypot(x-232.5,y-232.5)<=232);}
    }
    for(unsigned i=0;i<lv_obj_get_child_count(o);++i)visible_controls(lv_obj_get_child(o,i),area);
}
static void capture_view(const char *dir,const char *state) {
    capture(dir,state);
    if(refresh_button)circle(refresh_button);
    if(scroller){
        assert(lv_obj_hit_test(refresh_button,&(lv_point_t){283,418}));
        assert(!lv_obj_hit_test(refresh_button,&(lv_point_t){330,441}));
        assert(lv_obj_hit_test(add_control,&(lv_point_t){183,418}));
        assert(!lv_obj_hit_test(add_control,&(lv_point_t){135,441}));
        assert(!lv_obj_hit_test(add_control,&(lv_point_t){233,425}));
        assert(!lv_obj_hit_test(refresh_button,&(lv_point_t){233,425}));
    }
    for(int y=0;y<466;++y)for(int x=0;x<466;++x)if(hypot(x-232.5,y-232.5)>228)assert(pixels[y*466+x]==0);
    visible_controls(root,(lv_area_t){0,0,465,465});
}
static void populate(void) {
    stored=(stock_list_t){.count=8};
    for(unsigned i=0;i<8;++i)strcpy(stored.symbols[i],symbols[i]);
}
int main(int argc,char **argv) {
    const char *dir=argc>1?argv[1]:NULL;
    for(language=0;language<2;++language) {
        init();populate();read_ok=save_ok=online=true;busy=false;
        lv_obj_t *ring=lv_obj_get_child(lv_layer_top(),0);lv_obj_set_size(ring,450,450);lv_obj_center(ring);
        lv_obj_set_style_arc_width(ring,3,LV_PART_MAIN);lv_obj_set_style_arc_width(ring,3,LV_PART_INDICATOR);lv_obj_set_style_arc_color(ring,lv_color_hex(0x222326),LV_PART_MAIN);
        pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);lv_indev_set_display(pointer,display);lv_indev_set_read_cb(pointer,pointer_read);
        lv_obj_t *p=page("Stocks"),*header=lv_obj_get_child(lv_layer_top(),1);
        stock_test_title=header;lv_obj_set_style_text_font(header,&font_stocks_24,0);lv_obj_set_width(header,166);lv_obj_set_style_text_align(header,LV_TEXT_ALIGN_CENTER,0);lv_obj_align(header,LV_ALIGN_TOP_MID,0,56);
        lv_obj_t *back_button=lv_button_create(lv_layer_top());lv_obj_remove_style_all(back_button);lv_obj_set_pos(back_button,100,53);lv_obj_set_size(back_button,44,44);
        lv_obj_t *back_text=lv_label_create(back_button);lv_label_set_text(back_text,LV_SYMBOL_LEFT);lv_obj_set_style_text_font(back_text,&lv_font_montserrat_20,0);lv_obj_set_style_text_color(back_text,lv_color_hex(COL_TXT),0);lv_obj_center(back_text);
        unsigned before=requests;app_stocks.enter(p);
        assert(view==VIEW_LIST&&list.count==8&&requests==before+1&&mock_batch.list.count==8);
        assert(lv_obj_get_child_count(scroller)==8);
        capture_view(dir,"watchlist-loading");
        result(0,STOCK_READY);result(1,STOCK_READY);app_stocks.tick();
        assert(has_quote[0]&&has_quote[1]&&!has_quote[2]&&waiting);
        capture_view(dir,"watchlist-partial");
        finish();assert(!waiting);
        // 包括屏外的第4至8只都必须有真实报价，不是代码管理列表。/ Offscreen symbols must also receive actual quote data.
        for(unsigned i=0;i<8;++i) {
            assert(has_quote[i]&&row_states[i]==STOCK_READY&&!strcmp(cached[i].symbol,list.symbols[i]));
            assert(!strcmp(lv_label_get_text(rows[i].price),cached[i].price));
            assert(!strcmp(lv_label_get_text(rows[i].time),cached[i].timestamp+5));
            assert(strstr(lv_label_get_text(rows[i].code),cached[i].currency));
        }
        assert(strstr(lv_label_get_text(summary),"08"));
        assert(strstr(lv_label_get_text(page_label),"01 — 03"));
        assert(lv_obj_get_style_text_font(rows[0].name,0)==&font_stocks_22);
        assert(lv_obj_get_style_text_font(rows[0].price,0)==&font_stocks_31);
        assert(lv_obj_get_x(rows[0].name)>lv_obj_get_x(rows[1].name));
        assert(lv_obj_get_x(rows[2].name)>lv_obj_get_x(rows[1].name));
        capture_view(dir,"watchlist-overview");
        lv_obj_scroll_to_y(scroller,500,LV_ANIM_OFF);capture_view(dir,"watchlist-bottom");
        lv_obj_scroll_to_y(scroller,0,LV_ANIM_OFF);
        stock_quote_t preserved=cached[0];strcpy(cached[0].name,"\xf0\x9f\xa6\x84");render();assert(!strcmp(lv_label_get_text(rows[0].name),list.symbols[0]));cached[0]=preserved;render();
        before=requests;tap(rows[2].button);assert(view==VIEW_QUOTE&&requests==before);
        assert(!strcmp(lv_label_get_text(price),cached[2].price));capture_view(dir,"watchlist-detail");
        assert(app_stocks.back()&&view==VIEW_LIST&&requests==before);
        tap(rows[4].button);assert(strstr(lv_label_get_text(symbol),"CNY"));assert(app_stocks.back());
        stock_quote_t old=cached[5];before=requests;tap_point(330,441);assert(requests==before);tap_point(283,418);assert(requests==before+1&&mock_batch.list.count==8);
        result(5,STOCK_FAILED);finish();assert(row_states[5]==STOCK_FAILED&&has_quote[5]&&!memcmp(&old,&cached[5],sizeof old));
        assert(strstr(lv_label_get_text(rows[5].state),language?"旧":"Previous"));
        assert(strstr(lv_label_get_text(summary),"7 / 8"));
        capture_view(dir,"watchlist-failure");
        tap(refresh_button);before=requests;app_stocks.visibility(false);lv_tick_inc(60000);app_stocks.tick();assert(requests==before);
        app_stocks.visibility(true);assert(requests==before+1&&mock_batch.list.count==8);finish();
        before=requests;lv_tick_inc(REFRESH_MS-1);app_stocks.tick();assert(requests==before);lv_tick_inc(1);app_stocks.tick();assert(requests==before+1&&mock_batch.list.count==8);finish();
        pause_refresh();busy=true;begin_request();assert(waiting&&pending);before=requests;app_stocks.tick();assert(requests==before);
        busy=false;app_stocks.tick();assert(waiting&&!pending&&requests==before+1);finish();
        tap(refresh_button);result(0,STOCK_READY);app_stocks.tick();lv_tick_inc(REQUEST_WAIT_MS);app_stocks.tick();assert(!waiting&&row_states[0]==STOCK_READY&&row_states[7]==STOCK_FAILED&&has_quote[7]);
        before=requests;online=false;begin_request();assert(!waiting&&requests==before);
        for(unsigned i=0;i<8;++i)assert(row_states[i]==STOCK_OFFLINE&&has_quote[i]);
        online=true;app_stocks.tick();assert(waiting&&requests==before+1);finish();
        tap(rows[3].button);click_text(language?"删除":"Remove");assert(view==VIEW_DELETE&&delete_index==3);
        capture_view(dir,"watchlist-remove");
        save_ok=false;click_text(language?"确认移出":"Remove symbol");assert(list.count==8);
        save_ok=true;click_text(language?"确认移出":"Remove symbol");assert(view==VIEW_LIST&&list.count==7&&stored.count==7);
        assert(has_quote[3]&&!strcmp(cached[3].symbol,"hk80700"));finish();
        add_symbol(NULL);capture_view(dir,"watchlist-editor-numeric");
        tap(market_buttons[2]);lv_textarea_set_text(input,"00700");before=saves;save_symbol(NULL);assert(view==VIEW_EDITOR&&saves==before);
        tap(market_buttons[3]);lv_textarea_set_text(input,"AAPL");capture_view(dir,"watchlist-editor-us");
        save_ok=false;save_symbol(NULL);assert(list.count==7&&view==VIEW_EDITOR);save_ok=true;save_symbol(NULL);
        assert(list.count==8&&stored.count==8&&view==VIEW_LIST&&!strcmp(list.symbols[7],"usAAPL")&&mock_batch.list.count==8);finish();
        app_stocks.exit();lv_obj_clean(p);before=requests;read_ok=false;app_stocks.enter(p);
        assert(!loaded&&requests==before);before=saves;capture_view(dir,"watchlist-read-error");assert(saves==before);
        read_ok=true;reload(NULL);assert(loaded&&list.count==8&&mock_batch.list.count==8);finish();
        app_stocks.exit();lv_obj_clean(p);stored=(stock_list_t){0};before=requests;app_stocks.enter(p);
        assert(view==VIEW_LIST&&!list.count&&requests==before);capture_view(dir,"watchlist-empty");
        add_symbol(NULL);lv_textarea_set_text(input,"123");before=saves;save_symbol(NULL);assert(saves==before&&!list.count);
        lv_textarea_set_text(input,"600519");save_symbol(NULL);assert(view==VIEW_LIST&&list.count==1&&mock_batch.list.count==1);finish();
        assert(!app_stocks.back());app_stocks.exit();before=requests;lv_tick_inc(60000);app_stocks.tick();assert(!root&&requests==before);lv_deinit();
    }
    assert(cancels);
    puts("Eight live quote rows, default overview, incremental refresh, stale flags, markets and lifecycle passed");
    return 0;
}
