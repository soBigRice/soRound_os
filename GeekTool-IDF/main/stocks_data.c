#include "stocks_data.h"
#include "cJSON.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool stock_symbol_normalize(const char *input,char out[STOCK_SYMBOL_BYTES]) {
    if(!input || !out)return false;
    while(*input==' ')++input;
    size_t n=strlen(input);while(n && input[n-1]==' ')--n;
    if(n<3 || n>=STOCK_SYMBOL_BYTES)return false;
    char value[STOCK_SYMBOL_BYTES]={0};
    for(size_t i=0;i<n;++i){if((unsigned char)input[i]>=128)return false;value[i]=(char)tolower((unsigned char)input[i]);}
    bool us=value[0]=='u' && value[1]=='s';
    bool hk=value[0]=='h' && value[1]=='k';
    bool cn=value[0]=='s' && (value[1]=='h' || value[1]=='z');
    if(!us && !hk && !cn)return false;
    if((cn && n!=8) || (hk && n!=7) || (us && n>12))return false;
    for(size_t i=2;i<n;++i) {
        unsigned c=(unsigned char)value[i];
        if(us){if(c<'a' || c>'z')return false;value[i]=(char)toupper(c);}
        else if(c<'0' || c>'9')return false;
    }
    memcpy(out,value,sizeof value);return true;
}
static const char *field(const cJSON *row,int index) {
    const cJSON *item=cJSON_GetArrayItem(row,index);
    return cJSON_IsString(item)?item->valuestring:NULL;
}
static bool decimal(const char *text,bool positive,double *out) {
    if(!text || !*text || strlen(text)>=24)return false;
    const char *p=text;if(*p=='+' || *p=='-')++p;
    bool digits=false,dot=false;
    for(;*p;++p){if(*p=='.' && !dot){dot=true;continue;}if(*p<'0'||*p>'9')return false;digits=true;}
    if(!digits)return false;
    char *end=NULL;double value=strtod(text,&end);
    if(!end || *end || !isfinite(value) || fabs(value)>1e12 || (positive && value<=0))return false;
    *out=value;return true;
}
static bool utf8(const char *text) {
    const unsigned char *p=(const unsigned char *)text;
    while(*p){unsigned b=*p++,extra;unsigned cp;
        if(b<128){if(b<32 || b==127)return false;continue;}
        if(b>=0xc2 && b<=0xdf){cp=b&31;extra=1;}
        else if(b>=0xe0 && b<=0xef){cp=b&15;extra=2;}
        else if(b>=0xf0 && b<=0xf4){cp=b&7;extra=3;}else return false;
        unsigned count=extra;
        while(extra--){b=*p++;if((b&192)!=128)return false;cp=(cp<<6)|(b&63);}
        if((count==1 && cp<128)||(count==2 && cp<2048)||(count==3 && cp<65536)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return false;
    }return true;
}
static bool quote_time(const char *text,char out[20]) {
    if(!text)return false;
    char digits[15];size_t at=0,n=strlen(text);
    if(n!=14 && n!=19)return false;
    for(size_t i=0;i<n;++i) {
        if(n==19 && (i==4||i==7||i==10||i==13||i==16)) {
            if(i==10?text[i]!=' ':i==13||i==16?text[i]!=':':text[i]!='/'&&text[i]!='-')return false;
            continue;
        }
        if(text[i]<'0'||text[i]>'9'||at>=14)return false;
        digits[at++]=text[i];
    }
    if(at!=14)return false;
    digits[14]=0;
    int year=0,month=0,day=0,hour=0,minute=0,second=0;
    if(sscanf(digits,"%4d%2d%2d%2d%2d%2d",&year,&month,&day,&hour,&minute,&second)!=6)return false;
    const unsigned lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};
    bool leap=year%4==0 && (year%100!=0 || year%400==0);
    if(year<2020||year>2099||month<1||month>12||day<1||day>(int)lengths[month-1]+(month==2&&leap)||hour>23||minute>59||second>59)return false;
    snprintf(out,20,"%.4s-%.2s-%.2s %.2s:%.2s:%.2s",digits,digits+4,digits+6,digits+8,digits+10,digits+12);return true;
}
bool stock_quote_parse(const char *json,size_t bytes,const char *symbol,stock_quote_t *out) {
    char normalized[STOCK_SYMBOL_BYTES];
    if(!json||!out||!bytes||bytes>=STOCK_HTTP_BYTES||!stock_symbol_normalize(symbol,normalized)||strcmp(normalized,symbol))return false;
    cJSON *root=cJSON_ParseWithLengthOpts(json,bytes+1,NULL,true);if(!root)return false;
    const cJSON *code=cJSON_GetObjectItemCaseSensitive(root,"code"),*data=cJSON_GetObjectItemCaseSensitive(root,"data");
    const cJSON *entry=cJSON_GetObjectItemCaseSensitive(data,symbol),*qt=cJSON_GetObjectItemCaseSensitive(entry,"qt");
    const cJSON *row=cJSON_GetObjectItemCaseSensitive(qt,symbol);
    const char *name=field(row,1),*price=field(row,3),*change=field(row,31),*percent=field(row,32);
    const char *type=field(row,0),*identity=field(row,2);
    bool us=!strncmp(symbol,"us",2),hk=!strncmp(symbol,"hk",2);
    const char *currency=field(row,us?35:hk?75:82);
    const char *expected_type=us?"200":hk?"100":symbol[1]=='z'?"51":"1";
    double value=0,direction=0;stock_quote_t result={0};
    // 核对响应身份与真实行情时间；HTTP200、分时点或本机时间不能冒充报价成功。
    // Validate identity and source timestamp; HTTP 200, chart points or local time are not quotes.
    // 港股还有人民币等柜台，币种必须取报价字段，不能按市场猜测。
    // HK counters can quote in CNY: use the provider currency rather than guessing by market.
    bool valid=cJSON_IsNumber(code)&&code->valuedouble==0&&cJSON_IsArray(row)&&type&&!strcmp(type,expected_type)&&
        currency&&(!strcmp(currency,"CNY")||!strcmp(currency,"HKD")||!strcmp(currency,"USD"))&&
        identity && (us?(!strncmp(identity,symbol+2,strlen(symbol+2)) && (!identity[strlen(symbol+2)]||identity[strlen(symbol+2)]=='.')):!strcmp(identity,symbol+2))&&
        name&&*name&&strlen(name)<sizeof result.name&&utf8(name)&&decimal(price,true,&value)&&
        decimal(change,false,&direction)&&decimal(percent,false,&value)&&
        (direction==0?value==0:value==0||(direction>0)==(value>0))&&quote_time(field(row,30),result.timestamp);
    if(valid){
        snprintf(result.symbol,sizeof result.symbol,"%s",symbol);snprintf(result.name,sizeof result.name,"%s",name);
        snprintf(result.price,sizeof result.price,"%s",price);snprintf(result.change,sizeof result.change,"%s",change);
        snprintf(result.percent,sizeof result.percent,"%s",percent);result.direction=direction;
        snprintf(result.currency,sizeof result.currency,"%s",currency);*out=result;
    }
    cJSON_Delete(root);return valid;
}
