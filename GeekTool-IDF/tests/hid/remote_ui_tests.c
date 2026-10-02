#include "hid_sdk.h"
#include "app.h"
#include "settings.h"
#include <string.h>
#include <math.h>
#include "../../main/app_mouse.c"

int64_t host_time_us;
static uint8_t language;
uint8_t settings_lang(void) { return language; }
static uint32_t clock_ms(void) { return (uint32_t)(host_time_us/1000); }
static uint16_t buffer[466*466], pixels[466*466];
static lv_display_t *display;
static lv_obj_t *page, *heading;
static lv_point_t pointer;
static lv_indev_state_t pointer_state;
static lv_indev_t *indev;

static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *map) {
    int width=lv_area_get_width(a);
    for(int y=a->y1;y<=a->y2;y++) { memcpy(pixels+y*466+a->x1,map,width*2); map+=width*2; }
    lv_display_flush_ready(d);
}
static void read_pointer(lv_indev_t *input, lv_indev_data_t *data) {
    (void)input; data->point=pointer; data->state=pointer_state;
}
static void touch(int x, int y, bool pressed, int ms) {
    host_time_us+=(int64_t)ms*1000; pointer=(lv_point_t){x,y};
    pointer_state=pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
    lv_indev_read(indev); mouse_tick();
}
static void click(lv_obj_t *obj) { lv_obj_send_event(obj,LV_EVENT_CLICKED,NULL); }
static void step(void) { host_time_us+=40000; mouse_tick(); }
static void connected(void) {
    hid_test_connect(1); hid_test_encrypt(1);
    for(int i=0;i<3;i++) hid_test_subscribe(1,i,true);
    mouse_tick(); assert(s_ready);
}
static void enter(void) {
    hid_test_reset();
    page=lv_obj_create(lv_screen_active()); lv_obj_remove_style_all(page); lv_obj_set_size(page,466,466);
    ui_obj_set_scrollable(page,false); mouse_enter(page);
    lv_label_set_text(heading,tr_app_name("mouse"));
    lv_obj_align(heading,LV_ALIGN_TOP_MID,0,46);
}
static void leave(void) { mouse_exit(); lv_obj_delete(page); page=NULL; }

static void geometry(lv_obj_t *obj) {
    // 所有可见文本必须完整落在自己的控件内;不把不可见触控板背景当成圆屏按钮。
    if(lv_obj_check_type(obj,&lv_label_class)) {
        lv_area_t a,p; lv_obj_get_coords(obj,&a); lv_obj_get_coords(lv_obj_get_parent(obj),&p);
        if(!(a.x1>=p.x1 && a.x2<=p.x2 && a.y1>=p.y1 && a.y2<=p.y2))
            fprintf(stderr,"text '%s' bounds [%d,%d,%d,%d], parent [%d,%d,%d,%d]\n",lv_label_get_text(obj),a.x1,a.y1,a.x2,a.y2,p.x1,p.y1,p.x2,p.y2);
        assert(a.x1>=p.x1 && a.x2<=p.x2 && a.y1>=p.y1 && a.y2<=p.y2);
    }
    if(lv_obj_check_type(obj,&lv_button_class) && lv_obj_get_child_count(obj)==2) {
        lv_obj_t *icon=lv_obj_get_child(obj,0), *text=lv_obj_get_child(obj,1);
        if(lv_obj_check_type(icon,&lv_label_class) && lv_obj_check_type(text,&lv_label_class)) {
            lv_area_t a,b; lv_obj_get_coords(icon,&a); lv_obj_get_coords(text,&b);
            assert(a.y2<b.y1); // 图标与多行说明不能因英文点阵字体宽度而重叠。
        }
    }
    for(uint32_t i=0;i<lv_obj_get_child_count(obj);i++) geometry(lv_obj_get_child(obj,i));
}
static void snapshot(const char *directory, const char *name) {
    // lv_refr_now 只刷新绘制;还需实际推进 LVGL 定时器,才能观察切换后的选中动画。
    for(int i=0;i<12;i++) { host_time_us+=20000; lv_timer_handler(); mouse_tick(); }
    lv_obj_update_layout(page); geometry(page); lv_refr_now(display);
    for(int i=0;i<3;i++) {
        lv_area_t a; lv_obj_get_coords(g_modes[i],&a);
        const int xs[]={a.x1,a.x2},ys[]={a.y1,a.y2};
        for(int x=0;x<2;x++) for(int y=0;y<2;y++) assert(hypot(xs[x]-233,ys[y]-233)<222);
    }
    if(!directory) return;
    char file[512]; snprintf(file,sizeof file,"%s/%s.ppm",directory,name);
    FILE *f=fopen(file,"wb"); assert(f); fprintf(f,"P6\n466 466\n255\n");
    for(int y=0;y<466;y++) for(int x=0;x<466;x++) {
        uint16_t p=pixels[y*466+x];
        uint8_t rgb[]={((p>>11)&31)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};
        if(hypot(x-232.5,y-232.5)>232.5) memset(rgb,0,3);
        fwrite(rgb,1,3,f);
    }
    assert(fclose(f)==0);
}
int main(int argc,char **argv) {
    const char *directory=argc>1?argv[1]:NULL;
    lv_init(); lv_tick_set_cb(clock_ms); i18n_init();
    display=lv_display_create(466,466); lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,buffer,NULL,sizeof buffer,LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);
    lv_obj_set_style_bg_color(lv_screen_active(),lv_color_hex(COL_BG),0);
    // 仅重建现有系统框架以便看圆屏位置;三种模式的内容来自真实 app_mouse.c。
    lv_obj_t *ring=lv_arc_create(lv_layer_top()); lv_obj_set_size(ring,466,466); lv_obj_center(ring);
    lv_arc_set_bg_angles(ring,0,360); lv_arc_set_value(ring,80);
    lv_obj_set_style_arc_color(ring,lv_color_hex(0x15151a),LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring,lv_color_hex(COL_TXT),LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(ring,8,LV_PART_MAIN); lv_obj_set_style_arc_width(ring,8,LV_PART_INDICATOR);
    lv_obj_remove_style(ring,NULL,LV_PART_KNOB); ui_obj_set_clickable(ring,false);
    heading=lv_label_create(lv_layer_top()); lv_obj_set_style_text_font(heading,UI_FONT_L,0);
    lv_obj_set_style_text_color(heading,lv_color_hex(COL_TXT),0);
    indev=lv_indev_create(); lv_indev_set_type(indev,LV_INDEV_TYPE_POINTER); lv_indev_set_read_cb(indev,read_pointer);

    enter(); assert(!s_ready && lv_obj_has_state(g_drag,LV_STATE_DISABLED));
    snapshot(directory,"remote-pair-en");
    click(g_modes[HID_KEYBOARD]); assert(s_mode==HID_KEYBOARD && !s_ready);
    // 本地演示计时不依赖蓝牙;暂停、恢复、跨模式和一小时边界。
    click(lv_obj_get_parent(g_time)); host_time_us+=65000000; mouse_tick(); assert(strcmp(lv_label_get_text(g_time),"01:05")==0);
    click(lv_obj_get_parent(g_time)); host_time_us+=5000000; mouse_tick(); assert(strcmp(lv_label_get_text(g_time),"01:05")==0);
    click(g_modes[HID_MEDIA]); click(g_modes[HID_KEYBOARD]); mouse_tick(); assert(strcmp(lv_label_get_text(g_time),"01:05")==0);
    click(lv_obj_get_parent(g_time)); host_time_us+=3595000000LL; mouse_tick(); assert(strcmp(lv_label_get_text(g_time),"1:01:00")==0);
    click(lv_obj_get_child(g_body,-1)); mouse_tick(); assert(!s_timer_running && strcmp(lv_label_get_text(g_time),"00:00")==0);

    connected(); click(g_controls[1]); step(); assert(hid_packets[0].bytes[2]==0x4e);
    mouse_visibility(false); step(); assert(hid_packets[1].bytes[2]==0);
    mouse_visibility(true); mouse_tick(); click(g_modes[HID_MOUSE]); step();
    click(g_drag); assert(s_drag && hid_packets[hid_packet_count-1].bytes[0]==1);
    lv_obj_update_layout(page);
    touch(220,206,true,20); touch(230,216,true,20); touch(230,216,false,20);
    assert(hid_packets[hid_packet_count-1].bytes[0]==1 && hid_packets[hid_packet_count-1].bytes[1]==16);
    click(g_drag); assert(!s_drag && hid_packets[hid_packet_count-1].bytes[0]==0);
    // 原鼠标轻点/左右键/滚轮仍走真实输入处理。
    int before=hid_packet_count; touch(233,206,true,20); touch(233,206,false,20);
    assert(hid_packet_count==before+2 && hid_packets[before].bytes[0]==1 && hid_packets[before+1].bytes[0]==0);
    lv_obj_send_event(g_controls[2],LV_EVENT_PRESSED,NULL);
    lv_obj_send_event(g_controls[2],LV_EVENT_PRESS_LOST,NULL); assert(s_btns==0 && hid_packets[hid_packet_count-1].bytes[0]==0);
    touch(390,240,true,20); touch(390,216,true,20); touch(390,216,false,20);
    assert(hid_packets[hid_packet_count-1].bytes[3]==2);
    click(g_drag); mouse_visibility(false); step(); assert(!s_drag && hid_packets[hid_packet_count-1].bytes[0]==0);
    mouse_visibility(true); mouse_tick(); click(g_drag); click(g_modes[HID_MEDIA]); step();
    assert(!s_drag && hid_packets[hid_packet_count-1].bytes[0]==0 && s_ready);
    before=hid_advertisements; click(g_modes[HID_KEYBOARD]); click(g_modes[HID_MEDIA]); assert(hid_advertisements==before);
    for(int i=0;i<6;i++) { int n=hid_packet_count; click(g_controls[i]); step(); step(); assert(hid_packet_count==n+2); }
    for(int i=0;i<8;i++) click(g_controls[0]);
    click(g_controls[0]); assert(strcmp(lv_label_get_text(g_status),tr(S_REMOTE_BUSY))==0);
    clear_input(); step();

    for(int lang=0;lang<2;lang++) {
        leave(); language=lang; enter(); connected();
        for(int mode=0;mode<3;mode++) {
            click(g_modes[mode]); step();
            char name[64]; snprintf(name,sizeof name,"remote-%s-%s",mode==0?"mouse":mode==1?"slides":"media",lang?"zh":"en");
            snapshot(directory,name);
        }
    }
    // 断连应清掉拖拽状态;新连接不继承旧鼠标按键。
    click(g_modes[HID_MOUSE]); click(g_drag);
    struct ble_gap_event e={.type=BLE_GAP_EVENT_DISCONNECT,.disconnect={.conn={.conn_handle=1}}};
    hid_test_event(&e); mouse_tick(); assert(!s_drag && !s_ready);
    snapshot(directory,"remote-disconnected-zh");
    leave(); hid_start_fail=true;
    page=lv_obj_create(lv_screen_active()); lv_obj_remove_style_all(page); lv_obj_set_size(page,466,466);
    mouse_enter(page); assert(!s_started && !s_ready && strcmp(lv_label_get_text(g_status),tr(S_BT_FAIL))==0);
    leave();
    puts("remote UI: real mouse/tap/wheel/drag input, mode switching, all media controls, timer, hidden releases, reconnect, failure and bilingual round-screen layout passed");
}
