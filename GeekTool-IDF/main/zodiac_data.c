#include "zodiac_data.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
const char *const ZODIAC_KEYS[]={"aries","taurus","gemini","cancer","leo","virgo","libra","scorpio","sagittarius","capricorn","aquarius","pisces"};
const char *const ZODIAC_EN[]={"Aries","Taurus","Gemini","Cancer","Leo","Virgo","Libra","Scorpio","Sagittarius","Capricorn","Aquarius","Pisces"};
const char *const ZODIAC_ZH[]={"白羊座","金牛座","双子座","巨蟹座","狮子座","处女座","天秤座","天蝎座","射手座","摩羯座","水瓶座","双鱼座"};
static const char *const categories[]={"all","love","work","money","health"};
static bool string(const cJSON *object,const char *key,char *out,size_t cap,bool required) {
    const cJSON *item=cJSON_GetObjectItemCaseSensitive(object,key);
    if(!cJSON_IsString(item)||!item->valuestring||!item->valuestring[0])return !required;
    size_t length=strlen(item->valuestring);if(length>=cap)return false;
    const unsigned char *p=(const unsigned char *)item->valuestring;
    for(size_t at=0;at<length;) {
        unsigned b=p[at++],extra;uint32_t cp;
        if(b<128){cp=b;extra=0;}else if(b>=0xc2&&b<=0xdf){cp=b&31;extra=1;}
        else if(b>=0xe0&&b<=0xef){cp=b&15;extra=2;}else if(b>=0xf0&&b<=0xf4){cp=b&7;extra=3;}else return false;
        if(at+extra>length)return false;
        for(unsigned i=0;i<extra;++i){b=p[at++];if((b&192)!=128)return false;cp=(cp<<6)|(b&63);}
        if((extra==1&&cp<128)||(extra==2&&cp<2048)||(extra==3&&cp<65536)||cp>0x10ffff||
           (cp>=0xd800&&cp<=0xdfff)||cp<32||cp==127)return false;
    }
    memcpy(out,item->valuestring,length+1);return true;
}
bool zodiac_data_parse(const char *json,size_t length,unsigned sign,zodiac_data_t *out) {
    if(!json||!out||!length||length>=ZODIAC_HTTP_BYTES||sign>=ZODIAC_COUNT)return false;
    cJSON *root=cJSON_ParseWithLengthOpts(json,length+1,NULL,true);if(!root)return false;
    const cJSON *code=cJSON_GetObjectItemCaseSensitive(root,"code"),*data=cJSON_GetObjectItemCaseSensitive(root,"data");
    const cJSON *name=cJSON_GetObjectItemCaseSensitive(data,"name");
    zodiac_data_t *result=calloc(1,sizeof *result);if(!result){cJSON_Delete(root);return false;}
    memset(result->score,255,sizeof result->score);result->sign=(uint8_t)sign;
    bool valid=cJSON_IsNumber(code)&&code->valuedouble==200&&cJSON_IsObject(data)&&cJSON_IsString(name)&&
        !strcmp(name->valuestring,ZODIAC_KEYS[sign])&&string(data,"time",result->date,sizeof result->date,true)&&
        string(data,"shortcomment",result->comment,sizeof result->comment,true);
    const cJSON *fortune=cJSON_GetObjectItemCaseSensitive(data,"fortune"),*body=cJSON_GetObjectItemCaseSensitive(data,"fortunetext");
    for(unsigned i=0;i<5&&valid;++i) {
        const cJSON *score=cJSON_GetObjectItemCaseSensitive(fortune,categories[i]);
        if(cJSON_IsNumber(score)&&score->valuedouble>=0&&score->valuedouble<=5&&score->valuedouble==(int)score->valuedouble)
            result->score[i]=(uint8_t)score->valuedouble;
        valid=string(body,categories[i],result->body[i],sizeof result->body[i],i==0);
    }
    const cJSON *todo=cJSON_GetObjectItemCaseSensitive(data,"todo");
    valid=valid&&string(data,"luckycolor",result->color,sizeof result->color,false)&&
        string(data,"luckynumber",result->number,sizeof result->number,false)&&
        string(data,"luckyconstellation",result->partner,sizeof result->partner,false)&&
        string(todo,"yi",result->good,sizeof result->good,false)&&string(todo,"ji",result->avoid,sizeof result->avoid,false);
    if(valid)*out=*result;
    free(result);cJSON_Delete(root);return valid;
}
