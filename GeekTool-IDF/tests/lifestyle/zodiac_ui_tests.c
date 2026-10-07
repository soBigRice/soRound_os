#include "native_ui.h"
#include "zodiac_data.h"
static zodiac_state_t mock_state=ZODIAC_LOADING;
static bool online=true,busy;
static unsigned requests,cancelled;
static uint32_t token;
static zodiac_data_t fixture;
zodiac_state_t zodiac_fetch_begin(unsigned sign,uint32_t *out){if(!online)return ZODIAC_OFFLINE;if(busy)return ZODIAC_BUSY;++requests;*out=++token;fixture.sign=(uint8_t)sign;return ZODIAC_LOADING;}
zodiac_state_t zodiac_fetch_poll(uint32_t t,zodiac_data_t *out){assert(t==token);if(mock_state==ZODIAC_READY)*out=fixture;return mock_state;}
void zodiac_fetch_cancel(void){++cancelled;}
#include "../../main/app_zodiac.c"
int main(int argc,char **argv){const char *dir=argc>1?argv[1]:NULL;char json[8192];FILE *f=fopen(ZODIAC_FIXTURE_PATH,"rb");assert(f);size_t n=fread(json,1,sizeof json-1,f);fclose(f);json[n]=0;assert(zodiac_data_parse(json,n,0,&fixture));
 lv_init();f=fopen(ZODIAC_GLYPHS_PATH,"rb");assert(f);n=fread(json,1,sizeof json-1,f);fclose(f);json[n]=0;json[strcspn(json,"\n")]=0;assert(glyphs(json));lv_deinit();
 for(language=0;language<2;++language){s_sign=0;init();mock_state=ZODIAC_LOADING;lv_obj_t *p=page(language?"星座运势":"Zodiac");app_zodiac.enter(p);capture(dir,"zodiac-loading");circle(g_scroll);circle(g_action);unsigned before=requests;tap(g_action);assert(requests==before);
  mock_state=ZODIAC_READY;app_zodiac.tick();assert(s_ready&&!s_waiting);assert(strstr(lv_label_get_text(g_body),"幸运颜色：草绿"));capture(dir,"zodiac-daily");assert(lv_obj_get_height(g_body)>lv_obj_get_height(g_scroll));
  lv_obj_scroll_to_y(g_scroll,200,LV_ANIM_OFF);lv_refr_now(display);tap(g_tabs[1]);assert(s_category==1);capture(dir,"zodiac-love");assert(lv_obj_get_scroll_y(g_scroll)==0);
  before=requests;for(unsigned c=0;c<5;++c){tap(g_tabs[c]);assert(s_category==c);circle(g_tabs[c]);}assert(requests==before);tap(g_tabs[0]);
  tap(g_title);assert(s_selecting&&ui_obj_is_hidden(g_reading));capture(dir,"zodiac-picker");circle(g_picker);assert(app_zodiac.back()&&!s_selecting);assert(!app_zodiac.back());
  tap(g_title);lv_obj_t *list=lv_obj_get_child(g_picker,1);assert(lv_obj_get_child_count(list)==12);busy=true;tap(lv_obj_get_child(list,11));assert(s_sign==11&&s_waiting&&s_pending_start);app_zodiac.tick();assert(s_pending_start);busy=false;app_zodiac.tick();assert(s_waiting&&!s_pending_start);app_zodiac.tick();assert(s_ready);capture(dir,"zodiac-selected");
  online=false;tap(g_action);assert(!s_waiting&&!s_ready);capture(dir,"zodiac-offline");online=true;mock_state=ZODIAC_LOADING;tap(g_action);lv_tick_inc(8500);app_zodiac.tick();assert(!s_waiting);capture(dir,"zodiac-failed");
  s_day=today()-1;before=requests;mock_state=ZODIAC_LOADING;app_zodiac.tick();assert(s_waiting && requests==before+1);
  app_zodiac.tick();assert(requests==before+1);
  before=requests;app_zodiac.visibility(false);tap(g_action);assert(requests==before);app_zodiac.visibility(true);app_zodiac.exit();assert(!g_reading&&!g_scroll);lv_obj_delete(p);lv_deinit();
 }
 if(argc>2){lv_init();for(unsigned sign=0;sign<ZODIAC_COUNT;++sign){char path[1024];snprintf(path,sizeof path,"%s/horoscope-%s.json",argv[2],ZODIAC_KEYS[sign]);FILE *live=fopen(path,"rb");assert(live);size_t bytes=fread(json,1,sizeof json-1,live);fclose(live);json[bytes]=0;assert(zodiac_data_parse(json,bytes,sign,&fixture));assert(glyphs(fixture.comment));for(unsigned c=0;c<5;++c)assert(glyphs(fixture.body[c]));assert(glyphs(fixture.color)&&glyphs(fixture.number)&&glyphs(fixture.partner)&&glyphs(fixture.good)&&glyphs(fixture.avoid));}lv_deinit();puts("all 12 captured live API responses: real parser and 32px font coverage passed");}
 assert(cancelled>=6);puts("zodiac UI: native circular rendering and glyphs, all 12 sign controls, Chinese original, category/long-text scrolling, cancellation/busy handoff, offline/deadline, visibility and cleanup passed");}
