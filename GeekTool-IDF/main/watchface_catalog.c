#include "watchface.h"
#include "watchface_ui.h"

static const char *const themes[]={"TYPE","ORBIT","SHIFT","HAND"};
static const char *const kinds[]={"dots","bold","rings","weather","image"};
static const char *const hands[]={"mark","arc","numeral","orbit","frame","dots"};
static const char *const names[]={
    "TYPE / dots","TYPE / bold","TYPE / rings","TYPE / weather","TYPE / image",
    "ORBIT / dots","ORBIT / bold","ORBIT / rings","ORBIT / weather","ORBIT / image",
    "SHIFT / dots","SHIFT / bold","SHIFT / rings","SHIFT / weather","SHIFT / image",
    "HAND / mark","HAND / arc","HAND / numeral","HAND / orbit","HAND / frame","HAND / dots"
};
int watchface_count(void){return WATCHFACE_COUNT;}
const char *watchface_name(int index){return index>=0&&index<WATCHFACE_COUNT?names[index]:"";}
const char *watchface_theme_name(int theme){return theme>=0&&theme<WATCHFACE_THEME_COUNT?themes[theme]:"";}
int watchface_theme_for(int index){
    if(index<0||index>=WATCHFACE_COUNT)return -1;
    return index<WATCHFACE_LEGACY_COUNT?index/WATCHFACE_KIND_COUNT:WATCHFACE_LEGACY_THEME_COUNT;
}
const char *watchface_kind_name(int index){
    if(index<0||index>=WATCHFACE_COUNT)return "";
    return index<WATCHFACE_LEGACY_COUNT?kinds[index%WATCHFACE_KIND_COUNT]:hands[index-WATCHFACE_LEGACY_COUNT];
}
int watchface_index_in_theme(int theme,int current){
    if(theme<0||theme>=WATCHFACE_THEME_COUNT)return current;
    if(watchface_theme_for(current)==theme)return current;
    if(theme==WATCHFACE_LEGACY_THEME_COUNT)return WATCHFACE_LEGACY_COUNT;
    // Only the legacy groups share the five kinds. HAND has a different catalogue.
    int kind=current>=0&&current<WATCHFACE_LEGACY_COUNT?current%WATCHFACE_KIND_COUNT:0;
    return theme*WATCHFACE_KIND_COUNT+kind;
}
