#include "watchface_atelier.h"
#include "watchface_studio_draw.h"

#define ATELIER_TAU 6.2831853071795864769f

static wfs_point_t polar(float x, float y, float radius, float turn)
{
    return (wfs_point_t){x + sinf(turn * ATELIER_TAU) * radius,
                         y - cosf(turn * ATELIER_TAU) * radius};
}

static void digits(int value, char out[3])
{
    unsigned n = (unsigned)(value < 0 ? 0 : value) % 100u;
    out[0] = (char)('0' + n / 10);out[1] = (char)('0' + n % 10);out[2] = 0;
}

static void date(const wfs_draw_t *d, const watchface_data_t *data, float y, uint32_t color)
{
    char value[32];wfs_date(data, value, sizeof value);
    wfs_text(d, value, 233, y, 14, color, 2);
}

static void hands(const wfs_draw_t *d, const watchface_data_t *data,
                  float hour_length, float minute_length, uint32_t ink, bool outline)
{
    wfs_point_t h = polar(233, 233, hour_length, ((data->time.tm_hour % 12) + data->time.tm_min / 60.0f) / 12);
    wfs_point_t m = polar(233, 233, minute_length, (data->time.tm_min + data->time.tm_sec / 60.0f) / 60);
    // 深色底描边使指针在彩色对象上仍可读，不改变真实时分角度。
    // Dark outlines keep hands readable across colored objects without changing their real time angles.
    if (outline) {
        wfs_bar(d, 233, 233, h.x, h.y, 17, 0x000000);
        wfs_bar(d, 233, 233, m.x, m.y, 10, 0x000000);
    }
    wfs_bar(d, 233, 233, h.x, h.y, 11, ink);
    wfs_bar(d, 233, 233, m.x, m.y, 5, ink);
}

static void mono(const wfs_draw_t *d, const watchface_data_t *data)
{
    const uint32_t paper = 0xeeeae0;
    wfs_text(d, "12", 233, 132, 96, paper, -2);
    wfs_text(d, "3", 402, 266, 96, paper, 0);
    wfs_text(d, "6", 233, 428, 96, paper, 0);
    wfs_text(d, "9", 64, 266, 96, paper, 0);
    wfs_text(d, "M O N O", 233, 182, 12, 0x898980, 1);
    for (int n = 0; n < 12; ++n) {
        if (n % 3 == 0) continue;
        wfs_point_t a = polar(233, 233, 208, n / 12.0f);
        wfs_point_t b = polar(233, 233, 217, n / 12.0f);
        wfs_bar(d, a.x, a.y, b.x, b.y, 3, 0x85847b);
    }
    wfs_rect(d, 157, 311, 152, 31, 0, 0x000000);
    date(d, data, 333, 0xa09e93);
    hands(d, data, 93, 145, paper, true);
    wfs_point_t second = polar(233, 233, 171, data->time.tm_sec / 60.0f);
    wfs_bar(d, 233, 233, second.x, second.y, 1.5f, 0xe65b45);
    wfs_disc(d, second.x, second.y, 4, 0xe65b45);
    wfs_disc(d, 233, 233, 7, paper);
    wfs_disc(d, 233, 233, 2.5f, 0xe65b45);
}

static void bauhaus(const wfs_draw_t *d, const watchface_data_t *data)
{
    wfs_disc(d, 182, 173, 86, 0x2966b9);
    wfs_rect(d, 243, 204, 123, 142, 0, 0xd44d36);
    wfs_disc(d, 149, 319, 55, 0xe7b84a);
    wfs_disc(d, 126, 296, 45, 0x000000);
    wfs_bar(d, 233, 62, 233, 404, 1, 0x45453e);
    for (int n = 0; n < 12; ++n) {
        wfs_point_t p = polar(233, 233, 207, n / 12.0f);
        wfs_disc(d, p.x, p.y, n % 3 ? 2 : 4, 0xece6d6);
    }
    hands(d, data, 88, 151, 0xf2ecda, true);
    wfs_point_t s = polar(233, 233, 181, data->time.tm_sec / 60.0f);
    wfs_bar(d, 233, 233, s.x, s.y, 3.5f, 0x000000);
    wfs_bar(d, 233, 233, s.x, s.y, 1.5f, 0xf2ecda);
    wfs_disc(d, 233, 233, 8, 0x000000);
    wfs_disc(d, 233, 233, 4, 0xf2ecda);
    wfs_text(d, "BAUHAUS / FORM & TIME", 233, 85, 12, 0xc5bdab, 1);
    date(d, data, 397, 0xc5bdab);
}

static void segment(const wfs_draw_t *d, float x, float y, float length,
                    float width, bool vertical, uint32_t color)
{
    // 六点倒角构成实体数码管；复用有界多边形原语，避免引入字库或位图。
    // Six beveled vertices form solid LED segments using bounded polygons instead of new fonts or bitmaps.
    wfs_point_t p[6] = {{0, width / 2}, {width / 2, 0}, {length - width / 2, 0},
                        {length, width / 2}, {length - width / 2, width}, {width / 2, width}};
    for (unsigned i = 0; i < 6; ++i) {
        float a = p[i].x, b = p[i].y;
        p[i].x = x + (vertical ? b : a);p[i].y = y + (vertical ? a : b);
    }
    wfs_poly(d, p, 6, color);
}

static void seven_digit(const wfs_draw_t *d, int value, float x, float y)
{
    static const uint8_t bits[] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
    static const struct {uint8_t x, y;bool vertical;} positions[] = {
        {8,0,false}, {55,8,true}, {55,65,true}, {8,112,false},
        {0,65,true}, {0,8,true}, {8,56,false}
    };
    for (unsigned n = 0; n < 7; ++n)
        segment(d, x + positions[n].x, y + positions[n].y, 48, 9, positions[n].vertical,
                bits[value] & (1u << n) ? 0xffba60 : 0x2d241c);
}

static void seven(const wfs_draw_t *d, const watchface_data_t *data)
{
    wfs_text(d, "SEVEN / DIGITAL", 233, 106, 14, 0xb9ab94, 3);
    wfs_rect(d, 44, 142, 378, 184, 18, 0x171613);
    wfs_rect(d, 49, 147, 368, 174, 14, 0x0c0c0a);
    int values[] = {data->time.tm_hour / 10, data->time.tm_hour % 10,
                    data->time.tm_min / 10, data->time.tm_min % 10};
    const int x[] = {66, 142, 258, 334};
    for (int n = 0; n < 4; ++n) seven_digit(d, values[n], x[n], 172);
    wfs_rect(d, 229, 202, 9, 9, 2, 0xffba60);
    wfs_rect(d, 229, 254, 9, 9, 2, 0xffba60);
    for (int n = 0; n < 30; ++n)
        wfs_rect(d, 88 + n * 10, 342, 5, 5, 0, n <= data->time.tm_sec / 2 ? 0xb97c40 : 0x30261d);
    char second[3];digits(data->time.tm_sec, second);
    wfs_text(d, second, 233, 388, 30, 0xffba60, 2);
    date(d, data, 419, 0xb9ab94);
}

static void vinyl(const wfs_draw_t *d, const watchface_data_t *data)
{
    wfs_text(d, "VINYL / LONG PLAY", 233, 53, 12, 0xaaa5b4, 2);
    wfs_disc(d, 233, 245, 192, 0x101014);
    wfs_circle(d, 233, 245, 192, 1, 0x3c3a44);
    for (int n = 0; n < 11; ++n)
        wfs_circle(d, 233, 245, 103 + n * 8, n % 3 ? .6f : 1.2f, n % 3 ? 0x24232b : 0x35333e);
    wfs_disc(d, 233, 245, 98, 0xb2a1d7);
    wfs_text(d, "SIDE / A", 233, 191, 12, 0x383043, 2);
    wfs_disc(d, 233, 213, 7, 0x17151e);
    char value[8];wfs_time(data, value, sizeof value);
    wfs_text(d, value, 233, 279, 72, 0x201a2b, 0);
    char day[3];digits(data->time.tm_mday, day);
    wfs_text(d, day, 233, 314, 14, 0x383043, 2);
    wfs_disc(d, 358, 108, 16, 0x34333b);
    wfs_disc(d, 358, 108, 7, 0xbdb9bf);
    wfs_bar(d, 358, 108, 386, 163, 5, 0xbdb9bf);
    wfs_bar(d, 386, 163, 361, 291, 5, 0xbdb9bf);
    wfs_bar(d, 361, 291, 330, 324, 5, 0xbdb9bf);
    const wfs_point_t cartridge[] = {{326,312},{342,323},{327,343},{312,331}};
    wfs_poly(d, cartridge, 4, 0xd87551);
    wfs_point_t second = polar(233, 245, 184, data->time.tm_sec / 60.0f);
    wfs_disc(d, second.x, second.y, 4, 0xd87551);
}

static void prism(const wfs_draw_t *d, const watchface_data_t *data)
{
    const wfs_point_t left[] = {{233,70},{78,335},{233,350}};
    const wfs_point_t right[] = {{233,70},{388,335},{233,350}};
    const wfs_point_t cyan[] = {{233,70},{233,281},{139,231}};
    const wfs_point_t rose[] = {{233,281},{388,335},{283,154}};
    const wfs_point_t amber[] = {{78,335},{233,350},{233,281}};
    wfs_poly(d, left, 3, 0x306bc0);wfs_poly(d, right, 3, 0x7250b9);
    wfs_poly(d, cyan, 3, 0x62cbb7);wfs_poly(d, rose, 3, 0xd96d97);wfs_poly(d, amber, 3, 0xe6ac4e);
    // 中央负空间保留数字对比度，彩色面只承担构图，不编码虚假传感器读数。
    // Central negative space preserves numeral contrast; colored facets are composition, not sensor readings.
    wfs_rect(d, 65, 188, 336, 105, 0, 0x000000);
    char value[8];wfs_time(data, value, sizeof value);
    wfs_heavy_text(d, value, 233, 274, 96, 0xf2eee3, 0);
    wfs_text(d, "P R I S M", 233, 49, 13, 0xb4acbc, 1);
    for (int n = 0; n < 6; ++n) wfs_disc(d, 193 + n * 16, 376, 2, 0x51495b);
    wfs_disc(d, 193 + data->time.tm_sec / 59.0f * 80, 376, 3, 0xf2b26d);
    date(d, data, 417, 0xb4acbc);
}

static void metro(const wfs_draw_t *d, const watchface_data_t *data)
{
    wfs_text(d, "METRO / LOCAL TIME", 233, 66, 12, 0x97a9a4, 2);
    wfs_bar(d, 106, 111, 106, 350, 4, 0x8bc7a6);
    wfs_bar(d, 151, 375, 342, 375, 3, 0x6c9bd7);
    wfs_bar(d, 342, 375, 342, 128, 3, 0x6c9bd7);
    for (int n = 0; n < 3; ++n) {
        int y = 143 + n * 95;
        wfs_disc(d, 106, y, 8, 0x8bc7a6);wfs_disc(d, 106, y, 4, 0x000000);
    }
    wfs_disc(d, 342, 128, 6, 0x6c9bd7);
    wfs_disc(d, 151, 375, 6, 0x6c9bd7);
    wfs_disc(d, 106, 119 + data->time.tm_sec / 59.0f * 222, 4, 0xf2d077);
    wfs_text(d, "HOURS", 231, 124, 12, 0x8bc7a6, 3);
    wfs_text(d, "MINUTES", 231, 241, 12, 0x6c9bd7, 3);
    char hour[3], minute[3];digits(data->time.tm_hour, hour);digits(data->time.tm_min, minute);
    wfs_heavy_text(d, hour, 231, 213, 96, 0xe9e9df, 2);
    wfs_heavy_text(d, minute, 231, 330, 96, 0xe9e9df, 2);
    date(d, data, 420, 0x97a9a4);
}

static void moon_gate(const wfs_draw_t *d, const watchface_data_t *data)
{
    wfs_text(d, "MOON GATE", 233, 49, 13, 0xa7c4c8, 4);
    wfs_circle(d, 233, 227, 161, 2, 0x93b4ba);
    wfs_disc(d, 295, 154, 32, 0xe3d8b7);
    const wfs_point_t far[] = {{88,281},{168,171},{253,281}};
    const wfs_point_t middle[] = {{148,292},{257,192},{379,292}};
    const wfs_point_t near[] = {{88,275},{183,240},{267,288},{379,252},{379,307},{88,307}};
    wfs_poly(d, far, 3, 0x1c3e4b);wfs_poly(d, middle, 3, 0x285b68);wfs_poly(d, near, 6, 0x153440);
    // 黑色下沿切出月门底座；月亮是静态意象，不冒充实时月相。
    // A black lower edge forms the gate's sill; the moon is decorative rather than a live lunar phase.
    wfs_rect(d, 60, 307, 346, 106, 0, 0x000000);
    wfs_bar(d, 89, 307, 377, 307, 2, 0x93b4ba);
    wfs_point_t second = polar(233, 227, 169, -1.0f / 3 + data->time.tm_sec / 59.0f * (2.0f / 3));
    wfs_disc(d, second.x, second.y, 3, 0xe3d8b7);
    char value[8];wfs_time(data, value, sizeof value);
    wfs_text(d, value, 233, 390, 87, 0xe3e9df, 0);
    date(d, data, 425, 0x93b4ba);
}

static wfs_point_t ellipse(float turn, float rotation)
{
    float angle = turn * ATELIER_TAU, a = rotation * ATELIER_TAU;
    float x = cosf(angle) * 126, y = sinf(angle) * 67;
    return (wfs_point_t){286 + x * cosf(a) - y * sinf(a), 240 + x * sinf(a) + y * cosf(a)};
}

static void orrery(const wfs_draw_t *d, const watchface_data_t *data)
{
    const uint32_t color[] = {0x9e8ad5, 0x6aaeb6, 0xe6b368};
    const uint32_t track[] = {0x514164, 0x365d65, 0x635038};
    const float progress[] = {((data->time.tm_hour % 12) + data->time.tm_min / 60.0f) / 12,
                              (data->time.tm_min + data->time.tm_sec / 60.0f) / 60,
                              data->time.tm_sec / 60.0f};
    // 64段有界轨道不申请缓存；三颗星编码时分秒，不代表天体位置。
    // Sixty-four bounded segments need no cache; three planets encode clock time rather than celestial positions.
    for (int ring = 0; ring < 3; ++ring) {
        float rotation = (ring - 1) / 6.0f;
        wfs_point_t previous = ellipse(0, rotation);
        for (int n = 1; n <= 64; ++n) {
            wfs_point_t next = ellipse(n / 64.0f, rotation);
            wfs_bar(d, previous.x, previous.y, next.x, next.y, 1.2f, track[ring]);previous = next;
        }
        wfs_point_t p = ellipse(progress[ring], rotation);
        wfs_disc(d, p.x, p.y, ring == 2 ? 4 : 7, color[ring]);
    }
    wfs_disc(d, 286, 240, 12, 0xe0d9bf);
    wfs_disc(d, 286, 240, 5, 0xf6efdc);
    wfs_rect(d, 61, 147, 117, 190, 0, 0x000000);
    char hour[3], minute[3];digits(data->time.tm_hour, hour);digits(data->time.tm_min, minute);
    wfs_text(d, hour, 121, 230, 87, 0xeee9df, 0);
    wfs_text(d, minute, 121, 312, 87, 0xeee9df, 0);
    wfs_text(d, "O R R E R Y", 233, 61, 12, 0xb7b0c0, 1);
    wfs_text(d, "H / M / S", 233, 390, 12, 0xb7b0c0, 3);
    date(d, data, 423, 0x827f93);
}

void watchface_atelier_render(lv_layer_t *layer, const lv_area_t *area,
                             const watchface_data_t *data, int kind, bool preview)
{
    static const char *const names[] = {"MONO", "BAUHAUS", "SEVEN", "VINYL", "PRISM", "METRO", "MOON GATE", "ORRERY"};
    if (!data || !area || kind < 0 || kind >= 8) return;
    wfs_draw_t d = wfs_make(layer, area, preview);
    if (data->aod) {wfs_aod(&d, data, names[kind]);return;}
    switch (kind) {
        case 0: mono(&d, data); break;
        case 1: bauhaus(&d, data); break;
        case 2: seven(&d, data); break;
        case 3: vinyl(&d, data); break;
        case 4: prism(&d, data); break;
        case 5: metro(&d, data); break;
        case 6: moon_gate(&d, data); break;
        case 7: orrery(&d, data); break;
    }
}
