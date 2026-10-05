// 实际 app_weather HTTP 解析与 weather_ui 绘制,仅替换网络/FreeRTOS/设备。
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "app.h"
#include "weather_ui.h"
#include "lvgl_compat.h"
#include "src/misc/lv_text_private.h"
#include "esp_http_client.h"
#include "sdk.h"
#include "nvs.h"

static uint8_t language;
static bool online=true;
static int create_result=pdPASS, tasks;
static void (*pending_task)(void *);
static const char *response;
static size_t read_offset;
static bool init_fail;
static char requested_url[1024];
static int http_status=200;
static int request_buffer_size;
static esp_err_t open_result=ESP_OK;
static int64_t header_result;
static bool read_fail;
struct mock_client { int unused; };
static struct mock_client client;
static lv_obj_t *heading;
uint8_t settings_lang(void) { return language; }
void launcher_set_title(const char *text) { lv_label_set_text(heading,text); }
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap) { (void)ap; return online?ESP_OK:ESP_FAIL; }
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle) {
    (void)name; (void)stack; (void)arg; (void)priority; (void)handle;
    ++tasks; pending_task=fn; return create_result;
}
void vTaskDelete(void *task) { (void)task; }
int64_t esp_timer_get_time(void) { return (int64_t)lv_tick_get()*1000; }
esp_err_t esp_crt_bundle_attach(void *config) { (void)config; return ESP_OK; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
    assert(strstr(config->url,"weather_code,is_day"));
    snprintf(requested_url,sizeof requested_url,"%s",config->url);
    request_buffer_size=config->buffer_size_tx?config->buffer_size_tx:512;
    read_offset=0; return init_fail?NULL:&client;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t c,int size) {
    (void)size; assert(c);
    // ESP-IDF 6.0.1 builds the entire request line in the TX buffer before
    // sending. A small JSON fixture cannot exercise this URL-size constraint.
    const char *path=strchr(strstr(requested_url,"://")+3,'/'); assert(path);
    size_t line_size=strlen("GET ")+strlen(path)+strlen(" HTTP/1.1\r\n");
    return line_size<(size_t)request_buffer_size?open_result:ESP_FAIL;
}
int esp_http_client_get_status_code(esp_http_client_handle_t c) {(void)c;return http_status;}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c) { assert(c); return header_result; }
int esp_http_client_read(esp_http_client_handle_t c,char *out,int size) {
    assert(c); size_t left=strlen(response)-read_offset;
    if(read_fail && !left)return -1;
    size_t n=left<(size_t)size?left:(size_t)size;
    memcpy(out,response+read_offset,n); read_offset+=n; return (int)n;
}
esp_err_t esp_http_client_close(esp_http_client_handle_t c) { assert(c); return ESP_OK; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c) { assert(c); return ESP_OK; }
static bool frame_mode;
void display_weather_mode(bool enabled){frame_mode=enabled;}
#include "../../main/app_weather.c"

#define W 466
static uint16_t buffer[W*W],pixels[W*W];
static lv_display_t *display;
static lv_obj_t *page;
static unsigned renders, audited_labels;
static uint64_t flushed_pixels;
static int audit_section=-1;
static lv_point_t pointer;
static lv_indev_state_t pointer_state;
static lv_indev_t *input;
static void read_pointer(lv_indev_t *d,lv_indev_data_t *data) {(void)d;data->point=pointer;data->state=pointer_state;}
static void click_city(void) {
    lv_obj_update_layout(page);
    pointer=(lv_point_t){258,72};pointer_state=LV_INDEV_STATE_PRESSED;lv_tick_inc(20);lv_indev_read(input);
    pointer_state=LV_INDEV_STATE_RELEASED;lv_tick_inc(20);lv_indev_read(input);
    assert(s_choosing && weather_location_ui_visible());
}
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *map) {
    int width=lv_area_get_width(a);
    flushed_pixels+=(uint64_t)width*lv_area_get_height(a);
    for(int y=a->y1;y<=a->y2;++y) { memcpy(pixels+y*W+a->x1,map,(size_t)width*2); map+=width*2; }
    lv_display_flush_ready(d);
}
static void label_bounds(lv_obj_t *obj) {
    if(ui_obj_is_hidden(obj)) return;
    if(obj==s_details.hero && lv_obj_get_scroll_y(s_details.scroll)!=0)return; // Native scrolling clips the departing hero.
    // Roller option labels extend outside their clipping viewport by design.
    if(lv_obj_check_type(obj,&lv_roller_class)) return;
    if(lv_obj_check_type(obj,&lv_label_class)) {
        const lv_font_t *font=lv_obj_get_style_text_font(obj,LV_PART_MAIN);
        const char *text=lv_label_get_text(obj);
        uint32_t offset=0;
        while(text[offset]) {
            uint32_t cp=lv_text_encoded_next(text,&offset);
            lv_font_glyph_dsc_t glyph;
            assert(lv_font_get_glyph_dsc(font,&glyph,cp,0));
            assert(!glyph.is_placeholder);
        }
        lv_area_t a; lv_obj_get_coords(obj,&a);
        for(int i=0;i<4;++i) {
            int x=i&1?a.x2:a.x1,y=i&2?a.y2:a.y1;
            if(hypot(x-232.5,y-232.5)>223)
                fprintf(stderr,"out of safe circle: '%s' (%d,%d)\n",text,x,y);
            assert(hypot(x-232.5,y-232.5)<=223);
        }
    }
    for(uint32_t i=0;i<lv_obj_get_child_count(obj);++i) label_bounds(lv_obj_get_child(obj,i));
}
static void capture(const char *directory,const char *name) {
    lv_obj_update_layout(page); lv_obj_update_layout(lv_layer_top());
    label_bounds(page); label_bounds(heading);
    lv_obj_invalidate(page); lv_tick_inc(20); lv_timer_handler(); lv_refr_now(display); ++renders;
    assert(lv_obj_get_child_count(s_ui.icon)==0 && lv_obj_get_child_count(s_ui.temperature)==0);
    assert(lv_obj_get_child_count(s_details.hero)==7); // Hero points remain drawing objects, not per-dot widgets.
    if(!directory) return;
    char file[512]; snprintf(file,sizeof file,"%s/%s-%s.ppm",directory,name,language?"zh":"en");
    FILE *f=fopen(file,"wb"); assert(f); fprintf(f,"P6\n466 466\n255\n");
    for(int i=0;i<W*W;++i) {
        uint16_t p=pixels[i]; uint8_t rgb[]={((p>>11)&31)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};
        assert(fwrite(rgb,1,3,f)==3);
    }
    assert(fclose(f)==0);
    if(s_choosing || lv_obj_get_scroll_y(s_details.scroll)!=0) return;
    // 图标图集输出来自单独重绘的实际控件,不裁入旁边温度的度符号。
    ui_obj_set_hidden(s_ui.temperature,true); lv_refr_now(display);
    snprintf(file,sizeof file,"%s/%s-%s-icon.ppm",directory,name,language?"zh":"en");
    f=fopen(file,"wb"); assert(f); fprintf(f,"P6\n280 160\n255\n");
    for(int y=92;y<252;++y) for(int x=93;x<373;++x) {
        uint16_t p=pixels[y*W+x];
        uint8_t rgb[]={((p>>11)&31)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};
        assert(fwrite(rgb,1,3,f)==3);
    }
    assert(fclose(f)==0);
    ui_obj_set_hidden(s_ui.temperature,false); lv_refr_now(display);
}
static void request(int code,bool day,int temp) {
    static char json[512];
    // 已确认效果稿的三组样例,其余类型复用阴天数值以只比较图标。
    int low=code==0?22:code==61?20:21, high=code==0?30:code==61?25:28;
    int humidity=code==0?52:code==61?86:64;
    snprintf(json,sizeof json,
        "{\"current_units\":{\"temperature_2m\":\"C\",\"is_day\":\"\"},"
        "\"current\":{\"temperature_2m\":%d,\"relative_humidity_2m\":%d,\"weather_code\":%d,\"is_day\":%d},"
        "\"daily\":{\"temperature_2m_max\":[%d],\"temperature_2m_min\":[%d]}}",temp,humidity,code,day,high,low);
    response=json; start_fetch(); assert(s_task_alive); pending_task(NULL); weather_tick();
    assert(s_state==WX_OK && !s_task_alive);
    assert(s_ui.code==code && s_ui.is_day==day && s_ui.temp==temp && s_ui.has_data);
    assert(ui_obj_is_hidden(s_ui.status));
    assert(strcmp(lv_label_get_text(s_ui.condition),weather_condition_text(code,day))==0);
    char expected[96]; snprintf(expected,sizeof expected,"#90999F ↓# %d°    #90999F ↑# %d°",low,high);
    assert(strcmp(lv_label_get_text(s_ui.range),expected)==0);
    int cached_temp, cached_code; assert(weather_cached(&cached_temp,NULL,NULL,&cached_code,NULL));
    assert(cached_temp==temp && cached_code==code);
}
static void enter(void) {
    page=lv_obj_create(lv_screen_active()); lv_obj_remove_style_all(page); lv_obj_set_size(page,W,W);
    ui_obj_set_scrollable(page,false); weather_enter(page);assert(frame_mode);
}
static void advance(unsigned ms);
static void leave(void) { weather_exit();assert(!frame_mode);assert(!lv_anim_get(&s_details,NULL));lv_obj_delete(page);page=NULL;advance(1000); }
static uint64_t icon_pixels(void) {
    // 只比较图标像素,不让不同文案替一个错误复用的图标制造“通过”。
    uint64_t hash=UINT64_C(1469598103934665603);
    for(int y=92;y<252;++y) for(int x=93;x<373;++x) {
        hash^=pixels[y*W+x]; hash*=UINT64_C(1099511628211);
    }
    return hash;
}
static lv_obj_t *find_visible(lv_obj_t *obj,const char *text,const lv_obj_class_t *type) {
    if(ui_obj_is_hidden(obj))return NULL;
    if(type && lv_obj_check_type(obj,type))return obj;
    if(text && lv_obj_check_type(obj,&lv_label_class) && strcmp(lv_label_get_text(obj),text)==0)return lv_obj_get_parent(obj);
    for(uint32_t i=0;i<lv_obj_get_child_count(obj);++i) {lv_obj_t *found=find_visible(lv_obj_get_child(obj,i),text,type);if(found)return found;}
    return NULL;
}
static void select_roller(uint16_t parent,uint32_t id) {
    uint16_t list[64];size_t count=wx_location_children(parent,list,64);unsigned position=64;
    for(size_t i=0;i<count;++i)if(wx_locations[list[i]].id==id)position=(unsigned)i;
    assert(position<count);lv_obj_t *r=find_visible(page,NULL,&lv_roller_class);assert(r);
    lv_roller_set_selected(r,position,LV_ANIM_OFF);lv_obj_send_event(r,LV_EVENT_VALUE_CHANGED,NULL);
}
static void tap(const char *zh,const char *en) {
    lv_obj_t *b=find_visible(page,language?zh:en,NULL);assert(b);lv_obj_send_event(b,LV_EVENT_CLICKED,NULL);
}
static void advance(unsigned ms) {
    for(unsigned i=0;i<ms;i+=20) {lv_tick_inc(20);lv_timer_handler();lv_refr_now(display);}
}
static void draw_audit(lv_event_t *e) {
    lv_draw_task_t *task=lv_event_get_draw_task(e);
    lv_draw_label_dsc_t *d=lv_draw_task_get_label_dsc(task);if(!d)return;
    uint32_t offset=0;
    while(d->text[offset]) {
        uint32_t cp=lv_text_encoded_next(d->text,&offset);lv_font_glyph_dsc_t glyph;
        if(!lv_font_get_glyph_dsc(d->font,&glyph,cp,0)||glyph.is_placeholder) {
            fprintf(stderr,"detail missing glyph: U+%04x in %s\n",cp,d->text);assert(false);
        }
    }
    if(audit_section<0 || lv_event_get_target_obj(e)!=s_details.sections[audit_section])return;
    lv_area_t a;lv_draw_task_get_area(task,&a);++audited_labels;
    for(int k=0;k<4;++k) {
        int x=k&1?a.x2:a.x1,y=k&2?a.y2:a.y1;
        if(y<91 || hypot(x-232.5,y-232.5)>223) {
            fprintf(stderr,"detail outside safe circle: %s (%d,%d)\n",d->text,x,y);assert(false);
        }
    }
}
static char *load_forecast(void) {
    FILE *f=fopen(WX_FIXTURE_PATH,"rb");assert(f);assert(fseek(f,0,SEEK_END)==0);
    long length=ftell(f);assert(length>0 && length<WX_BUF-1);rewind(f);
    char *data=malloc((size_t)length+1);assert(data);assert(fread(data,1,(size_t)length,f)==(size_t)length);
    data[length]=0;fclose(f);return data;
}
static void parse_forecast_checks(const char *fixture) {
    weather_data_t d;
    assert(weather_data_parse(fixture,strlen(fixture),&d));
    assert(d.hour_count==12 && d.day_count==5 && d.temp==22 && d.humidity==80);
    assert(strcmp(d.updated,"13:15")==0 && strcmp(d.hours[11].time,"00:00")==0);
    assert(fabsf(d.wind-2.5f)<.01f && fabsf(d.visibility-5500)<.1f && isfinite(d.days[0].daylight));
    assert(!weather_data_parse(fixture,strlen(fixture)-1,&d));
    const char *partial="{\"current_units\":{\"apparent_temperature\":99},\"current\":{\"temperature_2m\":-12.3,\"relative_humidity_2m\":50,\"weather_code\":3,\"is_day\":0,\"wind_speed_10m\":null},\"daily\":{\"temperature_2m_min\":[-15],\"temperature_2m_max\":[-10]}}";
    assert(weather_data_parse(partial,strlen(partial),&d));
    assert(d.temp==-12 && isnan(d.apparent) && isnan(d.wind) && isnan(d.precipitation));
    assert(!d.hour_count && !d.day_count);
    const char *invalid="{\"current\":{\"temperature_2m\":22,\"relative_humidity_2m\":80,\"weather_code\":3,\"is_day\":2},\"daily\":{\"temperature_2m_min\":[20],\"temperature_2m_max\":[25]}}";
    assert(!weather_data_parse(invalid,strlen(invalid),&d));
    char url[1024];int length=weather_data_url(url,sizeof url,-90,-180);
    assert(length>0 && length<(int)sizeof url && strstr(url,"forecast_hours=12") && strstr(url,"forecast_days=5"));
    assert(strstr(url,"wind_speed_unit=ms") && strstr(url,"timezone=auto") && strstr(url,"visibility"));
    char tiny[20];assert(weather_data_url(tiny,sizeof tiny,31,121)>=(int)sizeof tiny && tiny[sizeof tiny-1]==0);
}
static void detail_checks(const char *directory,bool motion,const char *fixture) {
    response=fixture;assert(select_location(wx_location_find(3101)));pending_task(NULL);weather_tick();
    assert(s_details.available && s_details.data.hour_count==12 && s_details.data.day_count==5);
    assert(s_details.indicator_opa==0);
    lv_obj_update_layout(page);
    assert(lv_obj_get_scroll_bottom(s_details.scroll)==4*W);
    for(int i=0;i<4;++i) {
        ui_obj_set_send_draw_task_events(s_details.sections[i],true);
        lv_obj_add_event_cb(s_details.sections[i],draw_audit,LV_EVENT_DRAW_TASK_ADDED,NULL);
    }
    capture(directory,"forecast-hero");
    for(int i=0;i<4;++i) {
        lv_obj_scroll_to_y(s_details.scroll,(i+1)*W,LV_ANIM_OFF);advance(20);
        audit_section=i;char name[32];snprintf(name,sizeof name,"details-%d",i+1);capture(directory,name);audit_section=-1;
        assert(s_details.reveal[i]==1);
    }
    // Missing optional fields stay unavailable, never turn into fabricated zero values.
    request(3,true,26);lv_obj_scroll_to_y(s_details.scroll,W,LV_ANIM_OFF);capture(directory,"details-missing");
    response=fixture;start_fetch();pending_task(NULL);weather_tick();
    weather_details_reset(&s_details);weather_details_show(&s_details,&s_data,true);advance(1000);
    // Real pointer drag, release and native momentum rather than a synthetic scroll event.
    pointer=(lv_point_t){233,340};pointer_state=LV_INDEV_STATE_PRESSED;lv_indev_read(input);
    for(int i=0;i<8;++i) {pointer.y-=24;lv_tick_inc(20);lv_indev_read(input);lv_timer_handler();}
    int release_y=lv_obj_get_scroll_y(s_details.scroll);assert(release_y>0 && !s_choosing);
    pointer_state=LV_INDEV_STATE_RELEASED;lv_tick_inc(20);lv_indev_read(input);advance(100);
    assert(lv_obj_get_scroll_y(s_details.scroll)>release_y && s_details.indicator_opa>0);
    advance(1800);assert(lv_obj_get_scroll_y(s_details.scroll)<=4*W && s_details.indicator_opa==0);
    lv_obj_scroll_to_y(s_details.scroll,2*W,LV_ANIM_ON);advance(80);
    assert(s_details.indicator_opa>0);weather_visibility(false);
    assert(!lv_anim_get(&s_details,NULL));
    int paused=lv_obj_get_scroll_y(s_details.scroll);advance(1000);
    assert(lv_obj_get_scroll_y(s_details.scroll)==paused && s_details.indicator_opa==0);weather_visibility(true);
    lv_obj_scroll_to_y(s_details.scroll,4*W,LV_ANIM_ON);advance(40);
    weather_details_reset(&s_details);assert(!lv_anim_get(&s_details,NULL) && s_details.indicator_opa==0);
    weather_details_show(&s_details,&s_data,true);advance(700);assert(s_details.indicator_opa==0);
    // The fixed city hit area still opens the same location UI from a scrolled view.
    click_city();assert(weather_back());assert(!s_choosing);
    lv_obj_scroll_to_y(s_details.scroll,4*W,LV_ANIM_OFF);advance(1000);
    flushed_pixels=0;weather_tick();advance(100);assert(flushed_pixels==0);
    if(directory && motion && language) {
        weather_details_reset(&s_details);weather_details_show(&s_details,&s_data,true);
        // Single-page native scrolls, sampled every 40ms including capture's timer step.
        for(int i=0;i<120;++i) {
            if(i%30==0)lv_obj_scroll_to_y(s_details.scroll,(i/30+1)*W,LV_ANIM_ON);
            advance(20);char name[32];snprintf(name,sizeof name,"motion-%02d",i);capture(directory,name);
        }
    }
    // Restore the original location scenario so EN/ZH first-screen pixel baselines stay comparable.
    assert(select_location(wx_location_find(320104)));pending_task(NULL);weather_tick();
    printf("forecast details: live fixture, nullable fields, scoped JSON, circular glyph/layout, pointer inertia, fade, overlay pause and idle redraw passed (%s)\n",language?"ZH":"EN");
}
int main(int argc,char **argv) {
    if(argc==2 && strcmp(argv[1],"--lvgl-version")==0) {
        printf("%d.%d.%d\n",LVGL_VERSION_MAJOR,LVGL_VERSION_MINOR,LVGL_VERSION_PATCH);return 0;
    }
    const char *directory=argc>1?argv[1]:NULL;
    char *fixture=load_forecast();parse_forecast_checks(fixture);
    bool motion=argc>2 && strcmp(argv[2],"--motion")==0;
    const int codes[]={0,1,2,3,45,48,51,53,55,56,57,61,63,65,66,67,71,73,75,77,80,81,82,85,86,95,96,97,99};
    lv_init(); i18n_init();
    display=lv_display_create(W,W); lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(input,read_pointer);
    lv_display_set_buffers(display,buffer,NULL,sizeof buffer,LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);
    lv_obj_set_style_bg_color(lv_screen_active(),lv_color_hex(COL_BG),0);
    heading=lv_label_create(lv_layer_top()); lv_obj_set_style_text_font(heading,&font_location_24,0);
    lv_obj_set_width(heading,166);lv_label_set_long_mode(heading,LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(heading,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_color(heading,lv_color_hex(0xf2eee6),0); lv_obj_align(heading,LV_ALIGN_TOP_MID,0,56);
    lv_obj_t *ring=lv_arc_create(lv_layer_top()); lv_obj_set_size(ring,458,458); lv_obj_center(ring);
    ui_obj_set_clickable(ring,false);
    lv_arc_set_rotation(ring,270); lv_arc_set_bg_angles(ring,0,360); lv_arc_set_value(ring,6);
    lv_obj_remove_style(ring,NULL,LV_PART_KNOB);
    lv_obj_set_style_arc_width(ring,6,LV_PART_MAIN); lv_obj_set_style_arc_width(ring,6,LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ring,lv_color_hex(0x22272a),LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring,lv_color_hex(0xf2eee6),LV_PART_INDICATOR);
    lv_obj_t *back=lv_obj_create(lv_layer_top()); lv_obj_remove_style_all(back); lv_obj_set_size(back,40,40);
    lv_obj_set_style_radius(back,LV_RADIUS_CIRCLE,0); lv_obj_set_style_bg_opa(back,LV_OPA_COVER,0);
    lv_obj_set_style_bg_color(back,lv_color_hex(0x22272a),0); lv_obj_align(back,LV_ALIGN_TOP_MID,-100,52);
    lv_obj_t *arrow=lv_label_create(back); lv_obj_set_style_text_font(arrow,&lv_font_montserrat_20,0);
    lv_obj_set_style_text_color(arrow,lv_color_hex(0xf2eee6),0); lv_label_set_text(arrow,LV_SYMBOL_LEFT); lv_obj_center(arrow);
    for(language=0;language<2;++language) {
        uint64_t day_icons[29], night_icons[8];
        const int night_codes[]={0,1,2,80,81,82,85,86};
        tasks=0; s_task_alive=false; s_state=WX_IDLE; enter(); weather_tick();
        assert(tasks==1 && !s_ui.has_data); capture(directory,"loading");
        start_fetch(); assert(tasks==1); // 单任务保护仍有效。
        for(unsigned i=0;i<sizeof codes/sizeof codes[0];++i) {
            assert(weather_code_supported(codes[i]));
            s_task_alive=false; request(codes[i],true,codes[i]==61?23:codes[i]==0?28:26);
            char name[32]; snprintf(name,sizeof name,"code-%d-day",codes[i]); capture(directory,name);
            day_icons[i]=icon_pixels();
            for(unsigned previous=0;previous<i;++previous) assert(day_icons[i]!=day_icons[previous]);
        }
        for(int n=0;n<8;++n) {
            int code=night_codes[n];
            request(code,false,18); char name[32]; snprintf(name,sizeof name,"code-%d-night",code); capture(directory,name);
            night_icons[n]=icon_pixels();
            for(unsigned k=0;k<29;++k) if(codes[k]==code) assert(night_icons[n]!=day_icons[k]);
            for(int previous=0;previous<n;++previous) assert(night_icons[n]!=night_icons[previous]);
        }
        request(3,true,-12); capture(directory,"negative");
        request(3,true,100); capture(directory,"three-digit");
        assert(!weather_code_supported(-1) && !weather_code_supported(42) && !weather_code_supported(100));
        request(42,true,26); capture(directory,"unknown");
        online=false; start_fetch(); weather_tick(); assert(s_state==WX_OFFLINE);
        assert(!ui_obj_is_hidden(s_ui.status)); capture(directory,"offline");
        leave(); s_task_alive=false; enter(); weather_tick(); capture(directory,"offline-reenter");
        assert(!s_ui.has_data && !weather_cached(NULL,NULL,NULL,NULL,NULL));
        online=true; create_result=0; start_fetch(); weather_tick(); assert(!s_task_alive && s_state==WX_FAIL);
        create_result=pdPASS; init_fail=true; start_fetch(); pending_task(NULL); weather_tick();
        assert(s_state==WX_FAIL && !s_task_alive); init_fail=false;
        request(0,false,18); capture(directory,"recovered-night");
        // A late response from the previous city must never repopulate the cache.
        start_fetch(); assert(s_task_alive); uint32_t generation=s_generation;
        assert(select_location(wx_location_find(3201))); assert(s_generation==generation+1);
        assert(!weather_cached(NULL,NULL,NULL,NULL,NULL) && !s_ui.has_data);
        pending_task(NULL); assert(s_state==WX_IDLE); weather_tick(); assert(s_task_alive);
        assert(!weather_cached(NULL,NULL,NULL,NULL,NULL));
        pending_task(NULL);weather_tick();assert(s_state==WX_OK);
        assert(strstr(requested_url,"latitude=32."));
        choose_location(NULL);assert(s_choosing && weather_location_ui_visible());
        assert(weather_back());assert(!s_choosing && !weather_location_ui_visible());
        assert(wx_locations[wx_location_selected()].id==3201);
        mock_nvs_fail=true;assert(!select_location(wx_location_find(3101)));mock_nvs_fail=false;
        http_status=500;start_fetch();pending_task(NULL);weather_tick();assert(s_state==WX_FAIL);
        assert(strcmp(lv_label_get_text(s_ui.status),tr(S_WX_FETCH_FAIL))==0);
        assert(!weather_cached(NULL,NULL,NULL,NULL,NULL));capture(directory,"fetch-failed");http_status=200;
        open_result=ESP_ERR_TIMEOUT;start_fetch();pending_task(NULL);weather_tick();
        assert(s_state==WX_FAIL && !s_task_alive);open_result=ESP_OK;
        header_result=-ESP_ERR_HTTP_EAGAIN;start_fetch();pending_task(NULL);weather_tick();
        assert(s_state==WX_FAIL && read_offset==0);header_result=0;
        read_fail=true;start_fetch();pending_task(NULL);weather_tick();
        assert(s_state==WX_FAIL && !s_task_alive);read_fail=false;
        response="{\"current\":{\"temperature_2m\":null},\"daily\":{}}";
        start_fetch();pending_task(NULL);weather_tick();assert(s_state==WX_FAIL);
        request(80,false,20);
        click_city();capture(directory,"locations-common");
        tap("其他地点","Other location");capture(directory,"locations-province");
        select_roller(0,32);tap("下一步","Next");capture(directory,"locations-city");
        select_roller(wx_location_find(32),3201);tap("下一步","Next");capture(directory,"locations-district");
        select_roller(wx_location_find(3201),320104);tap("确认地址","Confirm");
        lv_timer_handler();assert(!s_choosing && wx_locations[wx_location_selected()].id==320104);
        assert(!weather_cached(NULL,NULL,NULL,NULL,NULL));pending_task(NULL);weather_tick();
        detail_checks(directory,motion,fixture);
        lv_obj_scroll_to_y(s_details.scroll,3*W,LV_ANIM_ON);advance(40);
        leave();
    }
    free(fixture);assert(audited_labels>100);
    printf("%u actual weather renders: all29 WMO codes, 8 night variants + location/cancel/stale-response/save-failure guards, EN/ZH, glyphs, round-safe layout, HTTP/day parsing, offline/re-entry/task recovery passed\n",renders);
    return 0;
}
