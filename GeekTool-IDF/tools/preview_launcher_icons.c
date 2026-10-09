// Native 466x466 launcher previews. The 74% battery is a visual fixture, not device telemetry.
#include "app.h"
#include "launcher_icons.h"
#include "weather_location_ui.h"
#include "tools_ui.h"
#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t language;
uint8_t settings_lang(void) {return language;}
static uint16_t frame[466*466];
_Alignas(LV_DRAW_BUF_ALIGN) static uint16_t buffer[466*48];
static lv_display_t *display;
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *bytes) {
    uint16_t *pixels=(uint16_t *)bytes;
    for(int y=a->y1;y<=a->y2;++y)for(int x=a->x1;x<=a->x2;++x)frame[y*466+x]=*pixels++;
    lv_display_flush_ready(d);
}
static const char *const names[]={"WiFi","I2C","System","weather","calendar","countdown","stopwatch","settings",
                                "ota","audio","level","maze","fluid","dice","mouse","twin","Answers","Zodiac","Merit","Pixels"};
_Static_assert(sizeof(names)/sizeof(names[0]) == LAUNCHER_ICON_COUNT, "Preview every registered icon");
static void battery(void) {
    lv_obj_t *ring=lv_arc_create(lv_layer_top());lv_obj_set_size(ring,458,458);lv_obj_center(ring);
    lv_arc_set_rotation(ring,270);lv_arc_set_bg_angles(ring,0,360);lv_arc_set_range(ring,0,100);lv_arc_set_value(ring,74);
    lv_obj_remove_style(ring,NULL,LV_PART_KNOB);ui_obj_set_clickable(ring,false);
    lv_obj_set_style_arc_width(ring,8,LV_PART_MAIN);lv_obj_set_style_arc_width(ring,8,LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ring,lv_color_hex(0x15151a),LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring,lv_color_hex(COL_TXT),LV_PART_INDICATOR);
}
static lv_obj_t *button;
static void screen(unsigned index) {
    lv_obj_clean(lv_screen_active());
    button=lv_obj_create(lv_screen_active());lv_obj_set_size(button,196,196);
    lv_obj_set_style_radius(button,98,0);lv_obj_set_style_border_width(button,2,0);
    lv_obj_set_style_border_color(button,lv_color_hex(LAUNCHER_FRAME_COLOR),0);
    lv_obj_set_style_bg_opa(button,LV_OPA_TRANSP,0);lv_obj_align(button,LV_ALIGN_CENTER,0,-16);
    ui_obj_set_scrollable(button,false);
    lv_obj_t *icon=launcher_icon_create(button,(launcher_icon_t)index);lv_obj_center(icon);
    lv_obj_t *name=lv_label_create(lv_screen_active());lv_label_set_text(name,tr_app_name(names[index]));
    lv_obj_set_style_text_font(name,&font_location_24,0);lv_obj_set_style_text_color(name,lv_color_hex(COL_TXT),0);
    lv_obj_set_width(name,300);lv_obj_set_style_text_align(name,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_align(name,LV_ALIGN_CENTER,0,112);
    lv_obj_t *left=launcher_icon_create(lv_screen_active(),LAUNCHER_PREV);lv_obj_align(left,LV_ALIGN_LEFT_MID,14,0);
    lv_obj_t *right=launcher_icon_create(lv_screen_active(),LAUNCHER_NEXT);lv_obj_align(right,LV_ALIGN_RIGHT_MID,-14,0);
}
static void export(const char *folder,const char *name) {
    memset(frame,0,sizeof frame);lv_obj_invalidate(lv_screen_active());lv_refr_now(display);
    char path[1024];snprintf(path,sizeof path,"%s/%s-%s.ppm",folder,name,language?"zh":"en");
    FILE *file=fopen(path,"wb");if(!file){perror(path);exit(1);}fprintf(file,"P6\n466 466\n255\n");
    for(unsigned i=0;i<466*466;++i) {
        uint16_t p=frame[i];uint8_t rgb[]={((p>>11)&31)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};
        fwrite(rgb,1,3,file);
    }
    if(fclose(file)){perror(path);exit(1);}
}
int main(int argc,char **argv) {
    if(argc!=2){fprintf(stderr,"usage: %s existing-output-folder\n",argv[0]);return 1;}
    lv_init();i18n_init();display=lv_display_create(466,466);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,buffer,NULL,sizeof buffer,LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
    lv_obj_set_style_bg_color(lv_screen_active(),lv_color_hex(COL_BG),0);battery();
    for(language=0;language<2;++language)for(unsigned i=0;i<LAUNCHER_ICON_COUNT;++i) {
        screen(i);export(argv[1],names[i]);
    }
    language=1;screen(LAUNCHER_SYSTEM);lv_obj_set_style_opa_layered(button,LV_OPA_50,0);export(argv[1],"system-fade");
    printf("Exported %u native launcher screens and one swap-opacity frame.\n",2u*LAUNCHER_ICON_COUNT);lv_deinit();return 0;
}
