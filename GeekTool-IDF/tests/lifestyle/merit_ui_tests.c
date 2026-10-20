#include "native_ui.h"
#include "merit_store.h"
#include "esp_heap_caps.h"
static bool artwork_memory_ok=true;
static void *artwork_malloc(size_t size,unsigned caps){return artwork_memory_ok?heap_caps_malloc(size,caps):NULL;}
static bool control,muted,load_ok=true,save_ok=true;
static uint32_t stored;
static unsigned sounds,writes,started,ended;
uint8_t settings_silent(void){return muted;}
bool buttons_control_pressed(void){bool hit=control;control=false;return hit;}
void buttons_reset_control(void){control=false;}
bool merit_load(uint32_t *out){if(!load_ok)return false;*out=stored;return true;}
bool merit_save(uint32_t count){++writes;if(!save_ok)return false;stored=count;return true;}
void audio_out_init(void){++started;}
void audio_out_deinit(void){++ended;}
void audio_out_knock(void){++sounds;}
#define heap_caps_malloc(size,caps) artwork_malloc(size,caps)
#include "../../main/app_merit.c"
#undef heap_caps_malloc
static void advance(unsigned ms){lv_tick_inc(ms);app_merit.tick();}
static uint16_t wood_reference[466*466];
static void remember_body(void){
 // 禁止空白基准让逐帧保留检查空过；木鱼下半身应有大量真实木纹像素。
 // Reject an empty baseline so per-frame retention cannot pass vacuously.
 unsigned lit=0;for(int y=270;y<390;++y)for(int x=90;x<377;++x)if(pixels[y*466+x])++lit;
 if(lit<10000)fprintf(stderr,"wood baseline missing: %u lit pixels\n",lit);
 assert(lit>=10000);memcpy(wood_reference,pixels,sizeof pixels);
}
_Alignas(LV_DRAW_BUF_ALIGN) static uint16_t second_buffer[466*40];
static void swapped_flush(lv_display_t *d,const lv_area_t *a,uint8_t *map){
 int w=lv_area_get_width(a);for(int y=a->y1;y<=a->y2;++y){
  for(int x=0;x<w;++x)pixels[y*466+a->x1+x]=(uint16_t)((map[x*2]<<8)|map[x*2+1]);
  map+=w*2;
 }lv_display_flush_ready(d);
}
static void merit_init(void){
 init();if(getenv("SOROUND_TEST_DEVICE_FORMAT")){
  lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565_SWAPPED);
  lv_display_set_buffers(display,buffer,second_buffer,sizeof buffer,LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display,swapped_flush);
 }
}
static void body_stays_visible(void){
 // 固定主体的每个亮像素都必须保留，避开上方运动木槌；虚影只能增加亮度。
 // Preserve every lit body pixel below the moving mallet; the separate echo may only add light.
 for(int y=270;y<390;++y)for(int x=90;x<377;++x){
  uint16_t before=wood_reference[y*466+x],after=pixels[y*466+x];
  bool retained=(after>>11)+1>=(before>>11)&&((after>>5)&63)+1>=((before>>5)&63)&&(after&31)+1>=(before&31);
  if(!retained)fprintf(stderr,"wood body moved or faded at (%d,%d), t=%lu: %04x -> %04x\n",x,y,(unsigned long)s_elapsed,before,after);
  assert(retained);
 }
}
static bool echo_visible(void){
 for(int y=270;y<390;++y)for(int x=78;x<388;++x)
  if(!wood_reference[y*466+x]&&pixels[y*466+x])return true;
 return false;
}
static void body_returns_to_idle(void){
 for(int y=270;y<390;++y)for(int x=78;x<388;++x)assert(pixels[y*466+x]==wood_reference[y*466+x]);
}
int main(int argc,char **argv){const char *dir=argc>1?argv[1]:NULL;
 for(language=0;language<2;++language){stored=0;muted=false;merit_init();lv_obj_t *p=page(language?"木鱼":"Merit");app_merit.enter(p);assert(s_art.data&&s_art.data_size==288*192*2);capture(dir,"merit");remember_body();circle(g_wood);circle(g_sound);circle(g_hint);
  unsigned before=sounds;for(int i=0;i<12;++i){tap(g_wood);assert(!strcmp(lv_label_get_text(g_plus),language?"功德+1":"MERIT +1"));assert(lv_obj_get_style_opa(g_plus,0)==255);capture(NULL,"retrigger-body");body_stays_visible();
   if(i==0){bool echo=false;for(unsigned frame=1;frame<=31;++frame){advance(20);capture(NULL,"single-body");body_stays_visible();echo|=echo_visible();}assert(echo);body_returns_to_idle();}
  }assert(s_count==12&&sounds==before+12&&!writes);
  capture(dir,"merit-motion-00");
  bool echo=false;for(unsigned frame=1;frame<=31;++frame){advance(20);char name[40];snprintf(name,sizeof name,"merit-motion-%02u",frame);capture(dir,name);body_stays_visible();echo|=echo_visible();if(frame==5)capture(dir,"merit-hit");}assert(echo);body_returns_to_idle();
  assert(lv_obj_get_style_opa(g_plus,0)==0);advance(1380);assert(writes==1&&stored==12);advance(5000);assert(writes==1);
  tap(g_sound);tap(g_wood);assert(s_count==13&&sounds==before+12);control=true;app_merit.tick();assert(s_count==14&&!control);app_merit.visibility(false);assert(stored==14);before=sounds;tap(g_wood);assert(s_count==14&&sounds==before);
  control=true;app_merit.visibility(true);app_merit.tick();assert(s_count==14&&!control);capture(NULL,"resumed-body");body_stays_visible();advance(620);capture(NULL,"resumed-idle");body_returns_to_idle();app_merit.exit();lv_obj_delete(p);assert(!g_wood&&!s_art.data);
  load_ok=false;p=page(language?"木鱼":"Merit");app_merit.enter(p);tap(g_wood);assert(s_count==0&&!s_storage_ready);assert(!strcmp(lv_label_get_text(g_count),"--"));load_ok=true;tap(g_wood);assert(s_count==14&&s_storage_ready);tap(g_wood);assert(s_count==15);
  save_ok=false;advance(2000);assert(s_dirty);app_merit.exit();lv_obj_delete(p);p=page(language?"木鱼":"Merit");app_merit.enter(p);assert(s_count==15&&s_dirty);save_ok=true;advance(2000);assert(stored==15&&!s_dirty);
  s_count=MERIT_MAX;update_count();tap(g_wood);assert(s_count==MERIT_MAX);capture(dir,"merit-max");circle(g_count);app_merit.exit();lv_obj_delete(p);
  muted=true;p=page(language?"木鱼":"Merit");app_merit.enter(p);before=sounds;tap(g_sound);tap(g_wood);assert(sounds==before&&!s_muted);app_merit.visibility(false);muted=false;app_merit.visibility(true);assert(!strcmp(lv_label_get_text(g_sound),language?"声音开启":"SOUND ON"));muted=true;unsigned prior=writes;for(unsigned i=0;i<110;++i){tap(g_wood);advance(200);}assert(writes==prior+1&&s_dirty);app_merit.exit();lv_obj_delete(p);
  artwork_memory_ok=false;p=page(language?"木鱼":"Merit");app_merit.enter(p);assert(!s_art.data);assert(!strcmp(lv_label_get_text(g_hint),language?"图片加载失败":"IMAGE UNAVAILABLE"));capture(dir,"merit-image-failed");app_merit.exit();lv_obj_delete(p);artwork_memory_ok=true;
  lv_deinit();writes=0;
 }
 assert(started==ended);puts("merit UI: real circular render/glyphs, rapid taps and PWR, animation, quiet/continuous save batching, load/save errors without losing RAM count, mute/visibility, overflow and cleanup passed");}
