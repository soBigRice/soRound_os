#include "answers_data.h"
#include "cJSON.h"
#include <string.h>

static bool title(char out[ANSWER_TEXT_BYTES], const cJSON *obj, const char *key) {
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(obj,key);
    if(!cJSON_IsString(v) || !v->valuestring)return false;
    const char *start=v->valuestring;
    while(*start==' ')++start;
    size_t length=strlen(start);
    while(length && start[length-1]==' ')--length;
    if(!length || length>=ANSWER_TEXT_BYTES)return false;
    // Reject malformed UTF-8 and embedded controls; never truncate a remote title mid-character.
    size_t at=0;unsigned count=0;
    while(at<length) {
        uint8_t b=(uint8_t)start[at++];uint32_t cp;unsigned extra;
        if(b<0x80){cp=b;extra=0;}
        else if(b>=0xc2 && b<=0xdf){cp=b&0x1f;extra=1;}
        else if(b>=0xe0 && b<=0xef){cp=b&0x0f;extra=2;}
        else if(b>=0xf0 && b<=0xf4){cp=b&7;extra=3;}
        else return false;
        if(at+extra>length)return false;
        for(unsigned i=0;i<extra;++i) {
            b=(uint8_t)start[at++];if((b&0xc0)!=0x80)return false;
            cp=(cp<<6)|(b&0x3f);
        }
        if((extra==1 && cp<0x80)||(extra==2 && cp<0x800)||(extra==3 && cp<0x10000)||
           cp>0x10ffff||(cp>=0xd800 && cp<=0xdfff)||cp<0x20||cp==0x7f)return false;
        if(++count>72)return false;
    }
    memcpy(out,start,length);out[length]=0;return true;
}

bool answers_data_parse(const char *json,size_t length,answer_response_t *out) {
    if(!json || !out || !length || length>=ANSWER_HTTP_BYTES)return false;
    cJSON *root=cJSON_ParseWithLengthOpts(json,length+1,NULL,true);
    if(!root)return false;
    const cJSON *code=cJSON_GetObjectItemCaseSensitive(root,"code");
    const cJSON *data=cJSON_GetObjectItemCaseSensitive(root,"data");
    answer_response_t answer={0};
    bool valid=cJSON_IsNumber(code)&&code->valuedouble==200&&cJSON_IsObject(data)&&
        title(answer.en,data,"title_en")&&title(answer.zh,data,"title_zh");
    if(valid)*out=answer;
    cJSON_Delete(root);return valid;
}
