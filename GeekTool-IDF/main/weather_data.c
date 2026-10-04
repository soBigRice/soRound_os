#include "weather_data.h"
#include "cJSON.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

int weather_data_url(char *out,size_t size,double lat,double lon) {
    return snprintf(out,size,"http://api.open-meteo.com/v1/forecast?latitude=%.5f&longitude=%.5f"
        "&current=temperature_2m,relative_humidity_2m,weather_code,is_day,apparent_temperature,precipitation,"
        "wind_speed_10m,wind_direction_10m,wind_gusts_10m,cloud_cover,pressure_msl,visibility"
        "&hourly=temperature_2m,precipitation_probability,weather_code,is_day,precipitation"
        "&daily=temperature_2m_max,temperature_2m_min,weather_code,sunrise,sunset,uv_index_max,"
        "precipitation_probability_max,precipitation_sum,daylight_duration&timezone=auto&forecast_days=5&forecast_hours=12&wind_speed_unit=ms",lat,lon);
}
static float number(const cJSON *obj,const char *key,int index,float min,float max) {
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(obj,key);
    if (index>=0) v=cJSON_IsArray(v)?cJSON_GetArrayItem(v,index):NULL;
    double n=cJSON_IsNumber(v)?v->valuedouble:NAN;
    return isfinite(n) && n>=min && n<=max ? (float)n : NAN;
}
static int code(const cJSON *obj,int index) {
    float n=number(obj,"weather_code",index,0,100);
    return isfinite(n) && floorf(n)==n ? (int)n : -1;
}
static const char *string(const cJSON *obj,const char *key,int index) {
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(obj,key);
    if(index>=0)v=cJSON_IsArray(v)?cJSON_GetArrayItem(v,index):NULL;
    return cJSON_IsString(v)?v->valuestring:NULL;
}
static void time_text(char out[6],const char *iso) {
    out[0]=0;
    if(!iso || strlen(iso)<16 || iso[10]!='T' || iso[13]!=':')return;
    if(iso[11]<'0'||iso[11]>'2'||iso[12]<'0'||iso[12]>'9'||iso[14]<'0'||iso[14]>'5'||iso[15]<'0'||iso[15]>'9')return;
    if((iso[11]-'0')*10+iso[12]-'0'>23)return;
    memcpy(out,iso+11,5);out[5]=0;
}
bool weather_data_parse(const char *json,size_t length,weather_data_t *out) {
    if(!json || !out || !length)return false;
    cJSON *root=cJSON_ParseWithLengthOpts(json,length+1,NULL,true);
    if(!root)return false;
    weather_data_t data={0};
    const cJSON *current=cJSON_GetObjectItemCaseSensitive(root,"current");
    const cJSON *daily=cJSON_GetObjectItemCaseSensitive(root,"daily");
    const cJSON *hourly=cJSON_GetObjectItemCaseSensitive(root,"hourly");
    float temp=number(current,"temperature_2m",-1,-100,100), hum=number(current,"relative_humidity_2m",-1,0,100);
    float low=number(daily,"temperature_2m_min",0,-100,100), high=number(daily,"temperature_2m_max",0,-100,100);
    float day=number(current,"is_day",-1,0,1);
    data.code=code(current,-1);
    data.valid=isfinite(temp)&&isfinite(hum)&&isfinite(low)&&isfinite(high)&&low<=high && data.code>=0 && (day==0 || day==1);
    if(data.valid) {
        data.temp=(int)lroundf(temp);data.low=(int)lroundf(low);data.high=(int)lroundf(high);data.humidity=(int)lroundf(hum);data.is_day=day!=0;
        time_text(data.updated,string(current,"time",-1));
        data.apparent=number(current,"apparent_temperature",-1,-120,120);
        data.wind=number(current,"wind_speed_10m",-1,0,150);
        data.gust=number(current,"wind_gusts_10m",-1,0,150);
        data.direction=number(current,"wind_direction_10m",-1,0,360);
        data.precipitation=number(current,"precipitation",-1,0,1000);
        data.cloud=number(current,"cloud_cover",-1,0,100);
        data.pressure=number(current,"pressure_msl",-1,800,1200);
        data.visibility=number(current,"visibility",-1,0,1000000);
        for(unsigned i=0;i<WX_HOURS;++i) {
            weather_hour_t *h=&data.hours[i];
            const char *stamp=string(hourly,"time",(int)i);if(!stamp)break;
            time_text(h->time,stamp);if(!h->time[0])break;
            h->temperature=number(hourly,"temperature_2m",(int)i,-100,100);
            h->probability=number(hourly,"precipitation_probability",(int)i,0,100);
            h->precipitation=number(hourly,"precipitation",(int)i,0,1000);
            h->code=code(hourly,(int)i);h->is_day=number(hourly,"is_day",(int)i,0,1)==1;
            ++data.hour_count;
        }
        for(unsigned i=0;i<WX_DAYS;++i) {
            weather_day_t *d=&data.days[i];
            const char *stamp=string(daily,"time",(int)i);if(!stamp || strlen(stamp)!=10)break;
            memcpy(d->date,stamp,11);time_text(d->sunrise,string(daily,"sunrise",(int)i));time_text(d->sunset,string(daily,"sunset",(int)i));
            d->low=number(daily,"temperature_2m_min",(int)i,-100,100);d->high=number(daily,"temperature_2m_max",(int)i,-100,100);
            if(d->low>d->high)d->low=d->high=NAN;
            d->probability=number(daily,"precipitation_probability_max",(int)i,0,100);
            d->precipitation=number(daily,"precipitation_sum",(int)i,0,1000);d->uv=number(daily,"uv_index_max",(int)i,0,30);
            d->daylight=number(daily,"daylight_duration",(int)i,0,86400);
            d->code=code(daily,(int)i);++data.day_count;
        }
        *out=data;
    }
    cJSON_Delete(root);return data.valid;
}
