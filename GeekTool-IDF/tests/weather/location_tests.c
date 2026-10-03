#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../main/weather_locations.c"
int main(void) {
    assert(wx_location_count==3257);
    assert(wx_location_children(0,NULL,0)==33);
    for(uint16_t i=1;i<wx_location_count;++i) {
        assert(wx_locations[i].parent<wx_location_count && wx_locations[i].parent!=i);
        assert(wx_location_find(wx_locations[i].id)==i);
        assert(wx_location_children(i,NULL,0)<=64);
        assert(wx_locations[i].lat_e5>0 && wx_locations[i].lon_e5>0);
        assert(*wx_location_name(i,true) && *wx_location_name(i,false));
    }
    assert(wx_locations[wx_location_find(330102)].parent==wx_location_find(3301));
    uint16_t sh=wx_location_find(3101),nj=wx_location_find(3201),hz=wx_location_find(3301);
    assert(wx_location_selected()==sh);
    // Independently checked against upstream coordtransform for the CSV's
    // Shanghai center (121.472644,31.231706), rounded to E5.
    assert(wx_locations[sh].lat_e5==3123364 && wx_locations[sh].lon_e5==12146812);
    assert(wx_location_select(nj));assert(wx_location_select(hz));assert(wx_location_select(nj));
    uint16_t list[3];assert(wx_location_recent(list)==2 && list[0]==nj && list[1]==hz);
    mock_nvs_fail=true;assert(!wx_location_select(sh) && wx_location_selected()==nj);mock_nvs_fail=false;
    initialized=false;selected=0;memset(recent,0,sizeof recent);
    assert(wx_location_selected()==nj);assert(wx_location_recent(list)==2 && list[0]==nj && list[1]==hz);
    assert(!wx_location_select(0) && !wx_location_select(wx_location_find(32)));
    puts("3256 locations, hierarchy/coordinates, stable-ID persistence, recent dedup and write-failure preservation passed");
}
