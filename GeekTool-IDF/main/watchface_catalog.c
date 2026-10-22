#include "watchface.h"
#include "watchface_ui.h"

static const char *const themes[]={"TYPE","ORBIT","SHIFT","HAND","STUDIO"};
static const char *const kinds[]={"dots","bold","rings","weather","image"};
static const char *const hands[]={"mark","arc","numeral","orbit","frame","dots"};
static const char *const studio[]={"radar","tape","iris","maze","fold","pixel","horizon","tide","growth","sector","solar","grid","petal","ribbon","binary","mono","bauhaus","seven","vinyl","prism","metro","moon-gate","orrery"};
static const char *const names[]={
    "TYPE / dots","TYPE / bold","TYPE / rings","TYPE / weather","TYPE / image",
    "ORBIT / dots","ORBIT / bold","ORBIT / rings","ORBIT / weather","ORBIT / image",
    "SHIFT / dots","SHIFT / bold","SHIFT / rings","SHIFT / weather","SHIFT / image",
    "HAND / mark","HAND / arc","HAND / numeral","HAND / orbit","HAND / frame","HAND / dots",
    "STUDIO / radar","STUDIO / tape","STUDIO / iris","STUDIO / maze","STUDIO / fold",
    "STUDIO / pixel","STUDIO / horizon","STUDIO / tide","STUDIO / growth",
    "STUDIO / sector","STUDIO / solar","STUDIO / grid","STUDIO / petal","STUDIO / ribbon","STUDIO / binary",
    "STUDIO / mono","STUDIO / bauhaus","STUDIO / seven","STUDIO / vinyl","STUDIO / prism","STUDIO / metro","STUDIO / moon-gate","STUDIO / orrery"
};
// 目录顺序即 NVS ID，新增组不得重新排列既有的 0..35。
// Catalogue order is the persisted NVS ID; keep existing indices 0..35 stable.
_Static_assert(sizeof names/sizeof names[0]==WATCHFACE_COUNT,"watchface catalogue/count mismatch");
_Static_assert(sizeof studio/sizeof studio[0]==WATCHFACE_STUDIO_COUNT,"studio catalogue/count mismatch");
_Static_assert(WATCHFACE_STUDIO_START==WATCHFACE_LEGACY_COUNT+WATCHFACE_HAND_COUNT,"persisted ID boundary changed");
int watchface_count(void){return WATCHFACE_COUNT;}
const char *watchface_name(int index){return index>=0&&index<WATCHFACE_COUNT?names[index]:"";}
const char *watchface_theme_name(int theme){return theme>=0&&theme<WATCHFACE_THEME_COUNT?themes[theme]:"";}
int watchface_theme_for(int index){
    if(index<0||index>=WATCHFACE_COUNT)return -1;
    if(index<WATCHFACE_LEGACY_COUNT)return index/WATCHFACE_KIND_COUNT;
    return index<WATCHFACE_STUDIO_START?WATCHFACE_LEGACY_THEME_COUNT:WATCHFACE_LEGACY_THEME_COUNT+1;
}
const char *watchface_kind_name(int index){
    if(index<0||index>=WATCHFACE_COUNT)return "";
    if(index<WATCHFACE_LEGACY_COUNT)return kinds[index%WATCHFACE_KIND_COUNT];
    return index<WATCHFACE_STUDIO_START?hands[index-WATCHFACE_LEGACY_COUNT]:studio[index-WATCHFACE_STUDIO_START];
}
int watchface_index_in_theme(int theme,int current){
    if(theme<0||theme>=WATCHFACE_THEME_COUNT)return current;
    if(watchface_theme_for(current)==theme)return current;
    if(theme==WATCHFACE_LEGACY_THEME_COUNT)return WATCHFACE_LEGACY_COUNT;
    if(theme==WATCHFACE_LEGACY_THEME_COUNT+1)return WATCHFACE_STUDIO_START;
    // 仅旧三组共用五种类型，HAND/STUDIO 切回时从该旧组第一款开始。
    // Only legacy groups share five kinds; HAND/STUDIO return to the first legacy kind.
    int kind=current>=0&&current<WATCHFACE_LEGACY_COUNT?current%WATCHFACE_KIND_COUNT:0;
    return theme*WATCHFACE_KIND_COUNT+kind;
}
