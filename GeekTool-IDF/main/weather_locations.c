#include "weather_locations.h"
#include "nvs.h"
#include <string.h>

static bool initialized;
static uint16_t selected;
static uint32_t recent[3];
uint16_t wx_location_find(uint32_t id) {
    for (uint16_t i=1;i<wx_location_count;++i) if(wx_locations[i].id==id) return i;
    return 0;
}
const char *wx_location_name(uint16_t i, bool chinese) {
    if(i>=wx_location_count) return "";
    return wx_location_names+(chinese?wx_locations[i].name:wx_locations[i].pinyin);
}
size_t wx_location_children(uint16_t parent,uint16_t *out,size_t cap) {
    size_t n=0;
    for(uint16_t i=1;i<wx_location_count;++i) if(wx_locations[i].parent==parent) {
        if(n<cap && out) out[n]=i;
        ++n;
    }
    return n;
}
void wx_location_init(void) {
    if(initialized) return;
    initialized=true; selected=wx_location_find(3101);
    // One versioned blob commits current + recent IDs together. Old NVS keys and
    // records survive firmware updates; invalid IDs fall back to Shanghai.
    uint32_t blob[5]={0}; size_t size=sizeof blob; nvs_handle_t n;
    if(nvs_open("weather",NVS_READONLY,&n)!=ESP_OK) return;
    if(nvs_get_blob(n,"locations",blob,&size)==ESP_OK && size==sizeof blob && blob[0]==1) {
        uint16_t i=wx_location_find(blob[1]);
        if(i && wx_locations[i].parent) selected=i;
        memcpy(recent,blob+2,sizeof recent);
    }
    nvs_close(n);
}
uint16_t wx_location_selected(void) { wx_location_init(); return selected; }
bool wx_location_select(uint16_t i) {
    wx_location_init();
    if(!i || i>=wx_location_count || !wx_locations[i].parent) return false;
    uint32_t next[3]={wx_locations[i].id,0,0}; unsigned used=1;
    for(unsigned j=0;j<3 && used<3;++j)
        if(recent[j] && recent[j]!=next[0] && wx_location_find(recent[j])) next[used++]=recent[j];
    uint32_t blob[5]={1,wx_locations[i].id,next[0],next[1],next[2]}; nvs_handle_t n;
    if(nvs_open("weather",NVS_READWRITE,&n)!=ESP_OK) return false;
    bool ok=nvs_set_blob(n,"locations",blob,sizeof blob)==ESP_OK && nvs_commit(n)==ESP_OK;
    nvs_close(n);
    if(ok) { selected=i; memcpy(recent,next,sizeof recent); }
    return ok;
}
size_t wx_location_recent(uint16_t out[3]) {
    wx_location_init(); size_t n=0;
    for(unsigned j=0;j<3;++j) { uint16_t i=wx_location_find(recent[j]); if(i) out[n++]=i; }
    return n;
}
