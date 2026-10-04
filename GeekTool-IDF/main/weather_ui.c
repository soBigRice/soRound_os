// 466×466 AMOLED 天气页:颜色/点距/轮廓是视觉语言,并非物理点阵屏。
// 图标与温度各只有一个绘制对象;状态变化才 invalidate,不为每个点分配 lv_obj。
#include "weather_ui.h"
#include "weather_artwork.h"
#include "app.h"
#include "settings.h"
#include "lvgl_compat.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define WX_PEARL  0xdce4e8u
#define WX_SLATE  0x8799a5u
#define WX_SHADOW 0x89aac0u
#define WX_AMBER  0xe4b967u
#define WX_SUNLIT 0xeed398u
#define WX_RAIN   0x789fb5u
#define WX_ICE    0xa6c6d0u
#define WX_IVORY  0xf2eee6u
#define WX_MUTED  0x90999fu
#define WX_PIN    0xcb5e58u
#define WX_MOON   0xb3afcfu
#define PI 3.14159265f

typedef enum {
    CLEAR, MAINLY_CLEAR, PARTLY_CLOUDY, OVERCAST, FOG, RIME_FOG,
    DRIZZLE, FREEZING_DRIZZLE, RAIN, FREEZING_RAIN,
    SNOW, SNOW_GRAINS, SHOWERS, SNOW_SHOWERS, THUNDER, HAIL
} icon_kind_t;
typedef struct {
    int code;
    icon_kind_t kind;
    unsigned intensity;
    const char *en, *zh;
} condition_t;

// 精确匹配 Open-Meteo WMO 枚举;不能把未知值、冻雨和雷暴全部归到普通雨。
static const condition_t CONDITIONS[] = {
    { 0, CLEAR, 1, "Clear", "晴天" },
    { 1, MAINLY_CLEAR, 1, "Mainly clear", "晴间多云" },
    { 2, PARTLY_CLOUDY, 1, "Partly cloudy", "多云" },
    { 3, OVERCAST, 1, "Overcast", "阴天" },
    { 45, FOG, 1, "Fog", "雾" },
    { 48, RIME_FOG, 1, "Rime fog", "冻雾" },
    { 51, DRIZZLE, 1, "Light drizzle", "小毛毛雨" },
    { 53, DRIZZLE, 2, "Drizzle", "中毛毛雨" },
    { 55, DRIZZLE, 3, "Dense drizzle", "密毛毛雨" },
    { 56, FREEZING_DRIZZLE, 1, "Freezing drizzle", "小冻毛毛雨" },
    { 57, FREEZING_DRIZZLE, 3, "Dense freezing drizzle", "密冻毛毛雨" },
    { 61, RAIN, 1, "Light rain", "小雨" },
    { 63, RAIN, 2, "Rain", "中雨" },
    { 65, RAIN, 3, "Heavy rain", "大雨" },
    { 66, FREEZING_RAIN, 1, "Freezing rain", "小冻雨" },
    { 67, FREEZING_RAIN, 3, "Heavy freezing rain", "大冻雨" },
    { 71, SNOW, 1, "Light snow", "小雪" },
    { 73, SNOW, 2, "Snow", "中雪" },
    { 75, SNOW, 3, "Heavy snow", "大雪" },
    { 77, SNOW_GRAINS, 1, "Snow grains", "雪粒" },
    { 80, SHOWERS, 1, "Light showers", "小阵雨" },
    { 81, SHOWERS, 2, "Rain showers", "中阵雨" },
    { 82, SHOWERS, 3, "Heavy showers", "强阵雨" },
    { 85, SNOW_SHOWERS, 1, "Snow showers", "小阵雪" },
    { 86, SNOW_SHOWERS, 3, "Heavy snow showers", "大阵雪" },
    { 95, THUNDER, 1, "Thunderstorm", "雷暴" },
    { 96, HAIL, 1, "Thunder with hail", "小冰雹雷暴" },
    { 97, THUNDER, 3, "Heavy thunderstorm", "强雷暴" },
    { 99, HAIL, 3, "Thunder with heavy hail", "大冰雹雷暴" },
};

static const condition_t *condition_for(int code) {
    for (unsigned i = 0; i < sizeof CONDITIONS / sizeof CONDITIONS[0]; ++i)
        if (CONDITIONS[i].code == code) return &CONDITIONS[i];
    return NULL;
}
bool weather_code_supported(int code) { return condition_for(code) != NULL; }
const char *weather_condition_text(int code, bool is_day) {
    const condition_t *c = condition_for(code);
    bool zh = settings_lang() != 0;
    if (!c) return zh ? "天气数据不可用" : "Weather unavailable";
    if (!is_day && code == 0) return zh ? "晴夜" : "Clear night";
    if (!is_day && code == 1) return zh ? "少云夜" : "Mainly clear night";
    if (!is_day && code == 2) return zh ? "多云夜" : "Partly cloudy night";
    return zh ? c->zh : c->en;
}

typedef struct { lv_layer_t *layer; lv_draw_rect_dsc_t dot; int x, y; } painter_t;
static painter_t painter(lv_event_t *e) {
    painter_t p = { .layer = lv_event_get_layer(e) };
    lv_area_t a; lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    p.x = a.x1; p.y = a.y1;
    lv_draw_rect_dsc_init(&p.dot);
    p.dot.radius = LV_RADIUS_CIRCLE;
    p.dot.bg_opa = LV_OPA_COVER;
    return p;
}
static void dot(painter_t *p, float x, float y, int diameter, uint32_t color) {
    int left = p->x + (int)lroundf(x - (diameter - 1) * .5f);
    int top = p->y + (int)lroundf(y - (diameter - 1) * .5f);
    lv_area_t a = { left, top, left + diameter - 1, top + diameter - 1 };
    p->dot.bg_color = lv_color_hex(color);
    lv_draw_rect(p->layer, &p->dot, &a);
}
static uint32_t blend(uint32_t a, uint32_t b, float t) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    uint32_t out = 0;
    for (int shift = 0; shift <= 16; shift += 8) {
        int av = (a >> shift) & 255, bv = (b >> shift) & 255;
        out |= (uint32_t)lroundf(av + (bv - av) * t) << shift;
    }
    return out;
}
static bool disk(float x, float y, float cx, float cy, float r) {
    return (x-cx)*(x-cx) + (y-cy)*(y-cy) <= r*r;
}
static void cloud(painter_t *p, float cx, float cy, float sx, float sy, bool dark) {
    // 从已确认效果稿的云瓣/圆底比例规范化为 28列×12行、9px 点距。
    // 上四行的两个区段是云瓣之间的外部凹口,后八行连续填充,没有内部孔洞。
    static const int8_t rows[12][4] = {
        {15,18,-1,-1}, {13,20,-1,-1}, {12,21,-1,-1}, {6,10,12,21},
        {5,22,-1,-1}, {3,25,-1,-1}, {2,26,-1,-1}, {1,27,-1,-1},
        {0,27,-1,-1}, {0,27,-1,-1}, {1,26,-1,-1}, {2,25,-1,-1}
    };
    for (int row = 0; row < 12; ++row) for (int col = 0; col < 28; ++col) {
        if (!((col>=rows[row][0] && col<=rows[row][1]) ||
              (rows[row][2]>=0 && col>=rows[row][2] && col<=rows[row][3]))) continue;
        int x=-120+col*9, y=-67+row*9;
        float light = (x+35)/100.f;
        if (light<0) light=0;
        if (light>1) light=1;
        light *= .60f + .40f*(32-y)/99.f;
        uint32_t color = blend(WX_SHADOW, WX_PEARL, light);
        if (dark) color = blend(0x566d7bu, color, .58f);
        // 小云的点径也随点距缩放,否则 0.58 倍云的圆点互相粘连成实心块。
        int diameter = (int)lroundf(6*fminf(sx,sy));
        if (diameter < 4) diameter = 4;
        dot(p, cx + x*sx, cy + y*sy, diameter, color);
    }
}
static void sun(painter_t *p, float cx, float cy, float radius) {
    int extent = (int)(radius / 8) * 8;
    for (int y = -extent; y <= extent; y += 8)
        for (int x = -extent; x <= extent; x += 8)
            if (disk(x,y,0,0,radius))
                dot(p,cx+x,cy+y,6,blend(WX_AMBER,WX_SUNLIT,(radius-y)/(2*radius)));
    for (int k = 0; k < 8; ++k) {
        float a = k*PI/4;
        for (int n = 0; n < 3; ++n) {
            float r = radius + 13 + n*8;
            dot(p,cx+cosf(a)*r,cy+sinf(a)*r,6,WX_SUNLIT);
        }
    }
}
static void moon(painter_t *p, float cx, float cy, float radius, bool star) {
    int extent = (int)(radius / 8) * 8;
    for (int y = -extent; y <= extent; y += 8)
        for (int x = -extent; x <= extent; x += 8)
            if (disk(x,y,0,0,radius) && !disk(x,y,radius*.48f,-radius*.20f,radius*.94f))
                dot(p,cx+x,cy+y,6,blend(WX_MOON,WX_IVORY,(radius-y)/(2*radius)));
    if (star) {
        dot(p,cx+radius+14,cy-14,4,WX_IVORY);
        for (int k = 0; k < 4; ++k)
            for (int n = 1; n <= 2; ++n)
                dot(p,cx+radius+14+cosf(k*PI/2)*n*5,cy-14+sinf(k*PI/2)*n*5,3,WX_IVORY);
    }
}
static void snowflake(painter_t *p, float cx, float cy, int size) {
    dot(p,cx,cy,4,WX_ICE);
    for (int k = 0; k < 6; ++k) {
        float a = k*PI/3;
        for (int n = 1; n <= 2; ++n)
            dot(p,cx+cosf(a)*size*n/2,cy+sinf(a)*size*n/2,3,WX_ICE);
    }
}
static unsigned particles(unsigned strength) { return strength == 1 ? 2 : strength == 2 ? 3 : 5; }
static void rain(painter_t *p, float cy, unsigned count, unsigned length, bool fine, bool frozen) {
    float step = count > 3 ? 27 : 40;
    for (unsigned i = 0; i < count; ++i) {
        float x = 140 + ((float)i - (count-1)*.5f)*step;
        for (unsigned n = 0; n < length; ++n)
            dot(p,x-n*4,cy+n*8,fine?4:6,frozen?WX_ICE:WX_RAIN);
        if (frozen && !fine) {
            dot(p,x-4,cy+8,4,WX_ICE);
            dot(p,x,cy+8,3,WX_ICE);
            dot(p,x-8,cy+8,3,WX_ICE);
        }
    }
    if (frozen) snowflake(p,140 + count*step*.5f + 7,cy+14,6);
}
static void snow(painter_t *p, unsigned count, bool grains) {
    float step = count > 3 ? 29 : 45;
    for (unsigned i = 0; i < count; ++i) {
        float x = 140 + ((float)i-(count-1)*.5f)*step;
        if (grains) dot(p,x,130,5,WX_ICE);
        else snowflake(p,x,128 + (i%2)*4,9);
    }
}
static bool bolt_mask(float x, float y) {
    // 闪电的两段斜楔与中间平台,用实心点阵表达,不退化成单条斜线。
    return (y >= 0 && y <= 22 && x >= 7-y*.95f && x <= 20-y*.55f) ||
           (y >= 17 && y <= 43 && x >= -5-(y-17)*.25f && x <= 17-(y-17)*.95f);
}
static void thunder(painter_t *p, unsigned intensity, bool hail) {
    cloud(p,140,72,.82f,.75f,true);
    float scale = intensity == 3 ? 1.13f : 1.f;
    for (int y = 0; y <= 44; y += 6) for (int x = -18; x <= 24; x += 6)
        if (bolt_mask(x,y)) dot(p,133+x*scale,104+y*scale,6,WX_AMBER);
    unsigned count = intensity == 3 ? 5 : 3;
    if (hail) {
        for (unsigned i = 0; i < count; ++i) {
            float x = 140 + ((float)i-(count-1)*.5f)*33;
            if (fabsf(x-140)<10) continue;
            dot(p,x,135,7,WX_ICE);
            dot(p,x-3,133,3,WX_PEARL);
        }
    } else {
        for (int side = -1; side <= 1; side += 2)
            for (unsigned stream = 0; stream < (intensity==3?2u:1u); ++stream)
                for (int n = 0; n < 3; ++n)
                    dot(p,140+side*(45+stream*24)-n*4,116+n*8,6,WX_RAIN);
    }
}
static void icon_event(lv_event_t *e) {
    weather_ui_t *ui = lv_event_get_user_data(e);
    painter_t p = painter(e);
    const condition_t *c = condition_for(ui->code);
    if (!ui->has_data || !c) {
        // 无数据不显示 0℃ 或太阳;三个灰点明确表示尚无可用天气图标。
        for (int n = -1; n <= 1; ++n) dot(&p,140+n*18,88,6,WX_SLATE);
        return;
    }
    const weather_artwork_t *art = weather_artwork_for(ui->code, ui->is_day);
    if (art) {
        lv_draw_image_dsc_t image;
        lv_draw_image_dsc_init(&image);
        image.src = art->image;
        lv_area_t area = {p.x + art->x, p.y + art->y,
                          p.x + art->x + art->image->header.w - 1,
                          p.y + art->y + art->image->header.h - 1};
        lv_draw_image(p.layer, &image, &area);
        return;
    }
    // Unknown/unmapped artwork falls back to the existing vector renderer.
    switch (c->kind) {
        case CLEAR:
            if (ui->is_day) sun(&p,140,75,35);
            else moon(&p,130,80,49,true);
            break;
        case MAINLY_CLEAR: case PARTLY_CLOUDY: {
            bool mainly = c->kind == MAINLY_CLEAR;
            if (ui->is_day) sun(&p,mainly?112:94,mainly?66:59,mainly?32:28);
            else moon(&p,mainly?111:95,60,40,false);
            cloud(&p,mainly?182:151,mainly?113:107,mainly?.58f:.87f,mainly?.58f:.82f,false);
            break;
        }
        case OVERCAST: cloud(&p,140,98,1,1,false); break;
        case FOG: case RIME_FOG:
            cloud(&p,140,67,.82f,.73f,false);
            for (int row = 0; row < 3; ++row)
                for (int x = -72; x <= 72; x += 8) dot(&p,140+x,107+row*16,5,WX_SLATE);
            if (c->kind == RIME_FOG) { snowflake(&p,49,121,7); snowflake(&p,231,129,7); }
            break;
        case DRIZZLE: case FREEZING_DRIZZLE:
            cloud(&p,140,77,.82f,.78f,false);
            rain(&p,119,particles(c->intensity),3,true,c->kind==FREEZING_DRIZZLE);
            break;
        case RAIN: case FREEZING_RAIN:
            cloud(&p,140,77,.82f,.78f,false);
            rain(&p,117,c->intensity==1?3:c->intensity==2?4:5,c->intensity==3?4:3,false,c->kind==FREEZING_RAIN);
            break;
        case SNOW: case SNOW_GRAINS:
            cloud(&p,140,77,.82f,.78f,false);
            snow(&p,c->kind==SNOW_GRAINS?5:particles(c->intensity),c->kind==SNOW_GRAINS);
            break;
        case SHOWERS: case SNOW_SHOWERS:
            if (ui->is_day) sun(&p,93,50,26);
            else moon(&p,94,49,33,false);
            cloud(&p,148,86,.84f,.73f,false);
            if (c->kind==SNOW_SHOWERS) snow(&p,particles(c->intensity),false);
            else rain(&p,117,c->intensity==1?3:c->intensity==2?4:5,c->intensity==3?4:3,false,false);
            break;
        case THUNDER: case HAIL: thunder(&p,c->intensity,c->kind==HAIL); break;
    }
}

// 温度专用字模:圆润的 2/6/8 与效果稿一致;不改计时器共用的 glyph_font5x7。
static const char *const DIGITS[10][7] = {
    {"01110","10001","10001","10001","10001","10001","01110"},
    {"00100","01100","00100","00100","00100","00100","01110"},
    {"01110","10001","00001","00110","01000","10000","11111"},
    {"11110","00001","00001","01110","00001","00001","11110"},
    {"00010","00110","01010","10010","11111","00010","00010"},
    {"11111","10000","10000","11110","00001","10001","01110"},
    {"01110","10000","10000","11110","10001","10001","01110"},
    {"11111","00001","00010","00100","01000","01000","01000"},
    {"01110","10001","10001","01110","10001","10001","01110"},
    {"01110","10001","10001","01111","00001","00001","01110"},
};
static void temp_event(lv_event_t *e) {
    weather_ui_t *ui = lv_event_get_user_data(e);
    painter_t p = painter(e);
    char text[16];
    if (ui->has_data) snprintf(text,sizeof text,"%d",ui->temp);
    else snprintf(text,sizeof text,"--");
    int n = (int)strlen(text);
    int pitch = n <= 3 ? 11 : 8;
    int diameter = pitch == 11 ? 7 : 5;
    int width = n*5*pitch + (n-1)*pitch + (ui->has_data?25:0);
    int x = (300-width)/2;
    for (int k = 0; k < n; ++k) {
        for (int row = 0; row < 7; ++row) for (int col = 0; col < 5; ++col) {
            bool on = text[k]=='-' ? row==3 : DIGITS[text[k]-'0'][row][col]=='1';
            if (on) dot(&p,x+col*pitch+pitch/2.f,row*pitch+pitch/2.f,diameter,WX_IVORY);
        }
        x += 6*pitch;
    }
    if (ui->has_data) {
        for (int k = 0; k < 8; ++k) {
            float a = k*PI/4;
            dot(&p,x+8+cosf(a)*9,11+sinf(a)*9,5,WX_IVORY);
        }
    }
}
static void pin_event(lv_event_t *e) {
    painter_t p = painter(e);
    lv_draw_triangle_dsc_t t; lv_draw_triangle_dsc_init(&t);
    t.color=lv_color_hex(WX_PIN); t.opa=LV_OPA_COVER;
    t.p[0]=(lv_point_precise_t){p.x+2,p.y+9};
    t.p[1]=(lv_point_precise_t){p.x+14,p.y+9};
    t.p[2]=(lv_point_precise_t){p.x+8,p.y+20};
    lv_draw_triangle(p.layer,&t);
    dot(&p,8,7,14,WX_PIN); dot(&p,8,7,5,COL_BG);
}
static lv_obj_t *drawing(lv_obj_t *parent, int width, int height, int y,
                         lv_event_cb_t callback, void *user) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o,width,height); lv_obj_align(o,LV_ALIGN_TOP_MID,0,y);
    ui_obj_set_scrollable(o,false); ui_obj_set_clickable(o,false);
    ui_obj_set_event_bubble(o,true); ui_obj_set_gesture_bubble(o,true);
    lv_obj_add_event_cb(o,callback,LV_EVENT_DRAW_MAIN,user);
    return o;
}
static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t col, int y) {
    lv_obj_t *o=lv_label_create(parent);
    lv_obj_set_style_text_font(o,font,0); lv_obj_set_style_text_color(o,lv_color_hex(col),0);
    lv_label_set_text(o,""); lv_obj_align(o,LV_ALIGN_TOP_MID,0,y);
    return o;
}
void weather_ui_create(weather_ui_t *ui, lv_obj_t *parent) {
    *ui=(weather_ui_t){ .code=-1, .is_day=true };
    ui->icon=drawing(parent,280,160,92,icon_event,ui);
    ui->temperature=drawing(parent,300,80,249,temp_event,ui);
    ui->condition=label(parent,&font_weather_20,WX_IVORY,335);
    ui->range=label(parent,&font_weather_20,WX_IVORY,371);
    lv_label_set_recolor(ui->range,true);
    ui->humidity=label(parent,&font_weather_16,WX_MUTED,410);
    ui->status=label(parent,&font_weather_16,WX_MUTED,335);
    lv_obj_set_width(ui->status,280);
    lv_obj_set_style_text_align(ui->status,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_t *pin=drawing(parent,18,22,62,pin_event,NULL);
    lv_obj_align(pin,LV_ALIGN_TOP_MID,66,62);
    weather_ui_status(ui,WEATHER_LOADING);
}
void weather_ui_show(weather_ui_t *ui, int temp, int low, int high,
                     int code, int humidity, bool is_day) {
    ui->code=code; ui->temp=temp; ui->is_day=is_day; ui->has_data=true;
    lv_obj_invalidate(ui->icon); lv_obj_invalidate(ui->temperature);
    lv_label_set_text(ui->condition,weather_condition_text(code,is_day));
    char text[96]; snprintf(text,sizeof text,"#90999F ↓# %d°    #90999F ↑# %d°",low,high);
    lv_label_set_text(ui->range,text);
    snprintf(text,sizeof text,"%s %d%%",settings_lang()?"湿度":"Humidity",humidity);
    lv_label_set_text(ui->humidity,text);
    ui_obj_set_hidden(ui->condition,false);
    ui_obj_set_hidden(ui->range,false);
    ui_obj_set_hidden(ui->humidity,false);
    ui_obj_set_hidden(ui->status,true);
}
void weather_ui_status(weather_ui_t *ui, weather_status_t status) {
    bool loading=status==WEATHER_LOADING;
    lv_label_set_text(ui->status,tr(loading?S_WX_LOADING:
        status==WEATHER_OFFLINE?S_WX_FAIL:S_WX_FETCH_FAIL));
    lv_obj_set_style_text_color(ui->status,lv_color_hex(loading?WX_MUTED:WX_PIN),0);
    ui_obj_set_hidden(ui->status,false);
    ui_obj_set_hidden(ui->condition,true);
    ui_obj_set_hidden(ui->range,true);
    ui_obj_set_hidden(ui->humidity,true);
}
