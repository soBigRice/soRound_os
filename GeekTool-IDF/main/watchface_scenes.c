#include "watchface_scenes.h"
#include "watchface_studio_draw.h"

#include <math.h>
#include <stdio.h>

#define SCENE_PI 3.14159265358979323846f
#define SCENE_TAU (2.0f * SCENE_PI)

static wfs_point_t polar(float radius, float angle, float cx, float cy)
{
    return (wfs_point_t){cx + sinf(angle) * radius, cy - cosf(angle) * radius};
}

typedef enum {
    DATE_FOLD, DATE_PIXEL, DATE_HORIZON, DATE_TIDE, DATE_GROWTH
} scene_date_t;

static void face_date(const wfs_draw_t *d, const watchface_data_t *data,
                      scene_date_t format, int y, int size, uint32_t color, float spacing)
{
    static const char *const days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char *const months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    static const char *const full_months[] = {"JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE", "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};
    const struct tm *t = &data->time;
    const char *day = days[LV_CLAMP(0, t->tm_wday, 6)];
    int month = LV_CLAMP(0, t->tm_mon, 11);
    char date[40];
    // 仅替换原稿中的日期数据，逐款保留原有日期次序、空格与分隔符。
    // Replace only the date data, preserving each design's order, spacing and separator.
    switch (format) {
        case DATE_FOLD:
            snprintf(date, sizeof(date), "%02d %s   /   %s", t->tm_mday, months[month], day);
            break;
        case DATE_PIXEL:
            snprintf(date, sizeof(date), "%02d %s  /  %s", t->tm_mday, months[month], day);
            break;
        case DATE_HORIZON:
            snprintf(date, sizeof(date), "%02d / %02d · %s", month + 1, t->tm_mday, day);
            break;
        case DATE_TIDE:
            snprintf(date, sizeof(date), "%02d %s / %s", t->tm_mday, full_months[month], day);
            break;
        case DATE_GROWTH:
            snprintf(date, sizeof(date), "%02d %s / %s", t->tm_mday, months[month], day);
            break;
        default:
            return;
    }
    wfs_text(d, date, 233, y, size, color, spacing);
}

static void two_digits(int value, char out[3])
{
    unsigned normalized = (unsigned)(value < 0 ? 0 : value) % 100u;
    out[0] = (char)('0' + normalized / 10u);
    out[1] = (char)('0' + normalized % 10u);
    out[2] = '\0';
}

static void maze(const wfs_draw_t *d, const watchface_data_t *data)
{
    const uint32_t paper = 0xe6e3d5, ink = 0x273229;
    wfs_disc(d, 233, 233, 233, paper);
    // 迷宫不代表真实路径；外圈小球与下方数值共同编码秒。
    // The maze is ornamental; the outer bead and its caption encode actual seconds.
    for (int row = 0; row < 4; ++row) {
        float radius = 108.0f + row * 23.0f;
        for (int k = 0; k < 5; ++k) {
            float start = (k / 5.0f + row * 0.027f) * 360.0f;
            float end = start + 55.8f;
            wfs_arc(d, 233, 233, radius, start - 90, end - 90, 3, ink);
            if ((k + row) % 2 == 0) {
                wfs_point_t a = polar(radius, end * SCENE_PI / 180.0f, 233, 233);
                wfs_point_t b = polar(radius + 23, end * SCENE_PI / 180.0f, 233, 233);
                wfs_bar(d, a.x, a.y, b.x, b.y, 3, ink);
            }
        }
    }
    for (int i = 0; i < 60; ++i) {
        bool major = i % 5 == 0;
        wfs_point_t a = polar(major ? 198 : 202, i * SCENE_TAU / 60.0f, 233, 233);
        wfs_point_t b = polar(207, i * SCENE_TAU / 60.0f, 233, 233);
        wfs_bar(d, a.x, a.y, b.x, b.y, major ? 2 : 1, 0x71776a);
    }
    wfs_disc(d, 233, 233, 88, paper);
    char hour[3], minute[3], caption[20];
    two_digits(data->time.tm_hour, hour);
    two_digits(data->time.tm_min, minute);
    wfs_heavy_text(d, hour, 233, 216, 94, 0x26352d, 0);
    wfs_heavy_text(d, minute, 233, 299, 94, 0x26352d, 0);
    wfs_bar(d, 195, 235, 271, 235, 1.5f, 0xc1c5b4);
    wfs_point_t bead = polar(189, data->time.tm_sec * SCENE_TAU / 60.0f, 233, 233);
    wfs_disc(d, bead.x, bead.y, 8, 0xd65c43);
    wfs_disc(d, bead.x, bead.y, 3, 0xf5d6b3);
    wfs_heavy_text(d, "MAZE / 60", 233, 76, 13, 0x4d5a4b, 3);
    snprintf(caption, sizeof(caption), "%02d SECONDS", data->time.tm_sec);
    wfs_heavy_text(d, caption, 233, 398, 12, 0x4d5a4b, 2);
}

static void fold_hinge(const wfs_draw_t *d, float x, float y)
{
    // SVG 的1px描边居中于10×13轮廓；LVGL描边在扩展半像素后的外框内部。
    // SVG centers its 1px stroke on the 10×13 contour; LVGL draws it inside the half-pixel-expanded frame.
    lv_draw_rect_dsc_t hinge;
    lv_draw_rect_dsc_init(&hinge);
    hinge.bg_color = lv_color_hex(0xb6bda3);
    hinge.border_color = lv_color_hex(0x4c5542);
    hinge.border_width = LV_MAX(1, wfs_scaled(d, 1));
    hinge.radius = wfs_scaled(d, 2.5f);
    lv_area_t area = {
        d->x + wfs_scaled(d, x - 0.5f), d->y + wfs_scaled(d, y - 0.5f),
        d->x + wfs_scaled(d, x + 10.5f) - 1, d->y + wfs_scaled(d, y + 13.5f) - 1
    };
    lv_draw_rect(d->layer, &hinge, &area);
}

static void fold(const wfs_draw_t *d, const watchface_data_t *data)
{
    wfs_disc(d, 233, 233, 233, 0x293128);
    wfs_circle(d, 233, 233, 211, 1, 0x59624e);
    wfs_heavy_text(d, "FOLD / TIME", 233, 66, 14, 0xb4bba4, 4);
    for (int i = 0; i < 2; ++i) {
        int y = i == 0 ? 92 : 239;
        wfs_rect(d, 91, y + 4, 284, 133, 14, 0x111a12);
        wfs_rect(d, 96, y, 274, 133, 11, 0xe7e3ce);
    }
    char hour[3], minute[3];
    two_digits(data->time.tm_hour, hour);
    two_digits(data->time.tm_min, minute);
    wfs_heavy_text(d, hour, 233, 211, 146, 0x293128, 0);
    wfs_heavy_text(d, minute, 233, 358, 146, 0x293128, 0);
    // 折痕与金属铰链保留纸牌构图；分钟切换只更新真实数字。
    // Creases and hinges retain the paper-card composition; minute changes update only real digits.
    for (int i = 0; i < 2; ++i) {
        int y = i == 0 ? 157 : 304;
        wfs_bar(d, 97, y, 369, y, 1.5f, 0x566046);
        wfs_bar(d, 97, y + 2, 369, y + 2, 1, 0xfffae7);
        for (int side = 0; side < 2; ++side) {
            int x = side == 0 ? 94 : 362;
            fold_hinge(d, x, y - 6);
        }
    }
    face_date(d, data, DATE_FOLD, 408, 15, 0xb4bba4, 2);
}

static const char *const pixel_digits[10][7] = {
    {"01110", "11011", "11011", "11011", "11011", "11011", "01110"},
    {"00110", "01110", "00110", "00110", "00110", "00110", "01111"},
    {"01110", "11011", "00011", "00110", "01100", "11000", "11111"},
    {"11110", "00011", "00011", "01110", "00011", "00011", "11110"},
    {"00011", "00111", "01111", "11011", "11111", "00011", "00011"},
    {"11111", "11000", "11000", "11110", "00011", "00011", "11110"},
    {"01110", "11000", "11000", "11110", "11011", "11011", "01110"},
    {"11111", "00011", "00110", "00110", "01100", "01100", "01100"},
    {"01110", "11011", "11011", "01110", "11011", "11011", "01110"},
    {"01110", "11011", "11011", "01111", "00011", "00011", "01110"}
};

static void pixel_time(const wfs_draw_t *d, const watchface_data_t *data)
{
    char value[8];
    wfs_time(data, value, sizeof(value));
    int x = 107;
    for (const char *ch = value; *ch; ++ch) {
        if (*ch == ':') {
            wfs_rect(d, x, 140, 8, 8, 0, 0xd5e9ac);
            wfs_rect(d, x + 10, 140, 8, 8, 0, 0xd5e9ac);
            wfs_rect(d, x, 160, 8, 8, 0, 0xd5e9ac);
            wfs_rect(d, x + 10, 160, 8, 8, 0, 0xd5e9ac);
            x += 30;
        } else if (*ch >= '0' && *ch <= '9') {
            for (int row = 0; row < 7; ++row) {
                for (int col = 0; col < 5; ++col) {
                    if (pixel_digits[*ch - '0'][row][col] == '1') {
                        wfs_rect(d, x + col * 10, 120 + row * 10, 8, 8, 0, 0xd5e9ac);
                    }
                }
            }
            x += 60;
        }
    }
}

static void pixel_camp(const wfs_draw_t *d, const watchface_data_t *data)
{
    wfs_disc(d, 233, 233, 233, 0x101b15);
    face_date(d, data, DATE_PIXEL, 80, 15, 0x9dae8b, 3);
    pixel_time(d, data);
    static const wfs_point_t stars[] = {{103, 228}, {151, 244}, {338, 227}, {376, 279}, {240, 228}};
    for (unsigned i = 0; i < sizeof(stars) / sizeof(stars[0]); ++i) {
        wfs_rect(d, stars[i].x, stars[i].y, 3, 3, 0, 0x879f72);
    }
    wfs_disc(d, 338, 259, 14, 0xb8cba1);
    wfs_disc(d, 344, 253, 14, 0x101b15);
    wfs_bar(d, 100, 354, 366, 354, 3, 0x6c805c);
    wfs_bar(d, 103, 357, 168, 357, 3, 0x6c805c);
    wfs_bar(d, 186, 357, 266, 357, 3, 0x6c805c);
    wfs_bar(d, 301, 357, 361, 357, 3, 0x6c805c);
    const wfs_point_t tent[] = {{177, 350}, {227, 278}, {281, 350}};
    const wfs_point_t door[] = {{227, 278}, {245, 350}, {208, 350}};
    wfs_poly(d, tent, 3, 0x8eaa79);
    wfs_poly(d, door, 3, 0x314c36);
    wfs_bar(d, 227, 278, 257, 350, 2, 0xb5c694);
    static const char *const cat[] = {"10001", "11111", "11011", "11111", "01110", "01110", "11111"};
    for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 5; ++col) {
            if (cat[row][col] == '1') {
                wfs_rect(d, 115 + col * 5, 315 + row * 5, 5, 5, 0, 0xc7d8aa);
            }
        }
    }
    // 奇偶秒只替换猫尾与火焰装饰帧，不宣称天气或养成数据。
    // Odd/even seconds change decorative tail/flame frames, not weather or pet telemetry.
    int tail_top = data->time.tm_sec % 2 ? 325 : 335;
    wfs_bar(d, 140, 345, 150, 345, 5, 0xc7d8aa);
    wfs_bar(d, 150, 345, 150, tail_top, 5, 0xc7d8aa);
    wfs_bar(d, 150, tail_top, 145, tail_top, 5, 0xc7d8aa);
    wfs_rect(d, 147.5f, 342.5f, 5, 5, 0, 0xc7d8aa);
    wfs_rect(d, 147.5f, tail_top - 2.5f, 5, 5, 0, 0xc7d8aa);
    wfs_bar(d, 291, 352, 311, 349, 4, 0x90795c);
    wfs_bar(d, 294, 348, 313, 352, 4, 0x90795c);
    int flame = data->time.tm_sec % 2 ? 4 : 0;
    const wfs_point_t fire[] = {
        {297, 345}, {297, 331}, {302, 331}, {302, 319 - flame},
        {307, 319 - flame}, {307, 327 - flame}, {312, 327 - flame}, {312, 345 - flame}
    };
    wfs_poly(d, fire, 8, 0xdb7f50);
    wfs_rect(d, 302, 334, 5, 11, 0, 0xeecb7c);
    char caption[20];
    snprintf(caption, sizeof(caption), "SECOND %02d", data->time.tm_sec);
    wfs_text(d, caption, 233, 402, 13, 0x9dae8b, 3);
}

static float quadratic(float a, float b, float c, float t)
{
    float u = 1.0f - t;
    return u * u * a + 2.0f * u * t * b + t * t * c;
}

static void clipped_sun(const wfs_draw_t *d, float cx, float cy, float radius, uint32_t color)
{
    wfs_point_t points[96];
    for (int i = 0; i < 96; ++i) {
        float a = i * SCENE_TAU / 96.0f;
        points[i] = (wfs_point_t){cx + cosf(a) * radius, cy + sinf(a) * radius};
    }
    // 日月靠近屏幕边缘时也通过同一圆形几何裁切。
    // Sun/moon silhouettes use the same circular geometry clipping near the screen edge.
    wfs_poly(d, points, 96, color);
}

static void horizon_band(const wfs_draw_t *d, int band, uint32_t color)
{
    static const float curves[4][7] = {
        {309, 115, 300, 233, 309, 351, 309},
        {332, 110, 314, 233, 331, 356, 325},
        {357, 129, 333, 258, 359, 387, 342},
        {389, 120, 357, 270, 389, 420, 378}
    };
    const float *v = curves[band];
    wfs_point_t points[51];
    int count = 0;
    for (int i = 0; i <= 24; ++i) {
        float t = i / 24.0f;
        points[count++] = (wfs_point_t){quadratic(0, v[1], v[3], t), quadratic(v[0], v[2], v[4], t)};
    }
    // SVG 的平滑二次曲线沿用前段镜像控制点。
    // The second quadratic segment mirrors the previous control point, matching the SVG's smooth curve.
    for (int i = 1; i <= 24; ++i) {
        float t = i / 24.0f;
        points[count++] = (wfs_point_t){quadratic(v[3], 2 * v[3] - v[1], 466, t),
                                      quadratic(v[4], 2 * v[4] - v[2], v[6], t)};
    }
    points[count++] = (wfs_point_t){466, 466};
    points[count++] = (wfs_point_t){0, 466};
    wfs_poly(d, points, count, color);
}

static void horizon(const wfs_draw_t *d, const watchface_data_t *data)
{
    int hour = data->time.tm_hour;
    bool day = hour >= 7 && hour < 19;
    float clock_hour = hour + data->time.tm_min / 60.0f;
    // 固定 07–19 点色盘切换仅为时间装饰，不计算当前位置的真实日出日落。
    // The fixed 07:00–19:00 palette is decorative and does not claim local astronomical sunrise/sunset.
    float progress = day ? (clock_hour - 7.0f) / 12.0f : fmodf(clock_hour + 5.0f, 24.0f) / 12.0f;
    progress = fmaxf(0, fminf(1, progress));
    float sx = 85 + 296 * progress, sy = 315 - 105 * sinf(SCENE_PI * progress);
    uint32_t sky = day ? 0x2b2824 : 0x171f30;
    wfs_disc(d, 233, 233, 233, sky);
    clipped_sun(d, sx, sy, 91, day ? 0xd69565 : 0xb6bca8);
    if (!day) clipped_sun(d, sx + 39, sy - 19, 84, sky);
    static const uint32_t day_bands[] = {0x9f6044, 0x754b3d, 0x503e35, 0x342f28};
    static const uint32_t night_bands[] = {0x384453, 0x2d3945, 0x24303a, 0x1b252e};
    for (int i = 0; i < 4; ++i) horizon_band(d, i, day ? day_bands[i] : night_bands[i]);
    char time[8];
    wfs_time(data, time, sizeof(time));
    wfs_text(d, "HORIZON", 233, 77, 14, 0xcdc6af, 4);
    wfs_text(d, time, 233, 218, 96, 0xf4ecda, -3);
    wfs_bar_opa(d, 152, 251, 314, 251, 0.8f, 0xf4ecda, 0.55f);
    wfs_text(d, day ? "DAYLIGHT HOURS" : "AFTER DARK", 233, 277, 12, 0xf4ecda, 3);
    face_date(d, data, DATE_HORIZON, 407, 15, 0xd8c7aa, 2);
}

static wfs_point_t bezier(wfs_point_t a, wfs_point_t b, wfs_point_t c, wfs_point_t e, float t)
{
    float u = 1 - t;
    float aa = u * u * u, bb = 3 * u * u * t, cc = 3 * u * t * t, ee = t * t * t;
    return (wfs_point_t){aa * a.x + bb * b.x + cc * c.x + ee * e.x,
                         aa * a.y + bb * b.y + cc * c.y + ee * e.y};
}

static void tide(const wfs_draw_t *d, const watchface_data_t *data)
{
    wfs_disc(d, 233, 233, 233, 0x101e28);
    char time[8], caption[16];
    wfs_time(data, time, sizeof(time));
    wfs_text(d, "TIDE", 233, 80, 15, 0xb7c7c8, 5);
    wfs_text(d, time, 233, 216, 99, 0xe8e9db, -3);
    face_date(d, data, DATE_TIDE, 252, 13, 0x9cbbbd, 2);
    // 波纹只随秒值呼吸，没有潮汐数据源或海平面读数。
    // Waves breathe with clock seconds; there is no tide data source or sea-level measurement.
    for (int k = 0; k < 7; ++k) {
        float y = 289 + k * 19;
        float offset = sinf(data->time.tm_sec * SCENE_TAU / 60.0f + k * 0.7f) * 9;
        const wfs_point_t points[] = {
            {-30, y}, {50, y - 24 + offset}, {105, y + 24 + offset}, {185, y},
            {265, y - 24 - offset}, {320, y - 24 - offset}, {390, y},
            {460, y + 24 + offset}, {465, y + 23}, {500, y}
        };
        float width = k == 0 ? 2 : 1.5f;
        uint32_t color = k == 0 ? 0x92b9ba : 0x48787d;
        float opacity = 1 - k * 0.09f;
        for (int segment = 0; segment < 3; ++segment) {
            const wfs_point_t *p = points + segment * 3;
            wfs_point_t previous = p[0];
            for (int step = 1; step <= 32; ++step) {
                wfs_point_t current = bezier(p[0], p[1], p[2], p[3], step / 32.0f);
                // 公共线原语按圆边与线宽裁切，波纹在缩略图角落不会漏出。
                // The shared primitive clips to the circle and line width, keeping preview corners clear.
                wfs_bar_opa(d, previous.x, previous.y, current.x, current.y, width, color, opacity);
                previous = current;
            }
        }
    }
    float progress = data->time.tm_sec / 59.0f;
    float bx = 86 + progress * 294, by = 289 + sinf(progress * SCENE_TAU) * 8;
    wfs_disc(d, bx, by, 5, 0xeea577);
    wfs_bar(d, bx, by - 16, bx, by - 5, 1, 0xeea577);
    snprintf(caption, sizeof(caption), "%02d / 60", data->time.tm_sec);
    wfs_text(d, caption, 233, 410, 15, 0xb7c7c8, 3);
}

static void growth(const wfs_draw_t *d, const watchface_data_t *data)
{
    wfs_disc(d, 233, 233, 233, 0x242820);
    // 年轮是固定装饰纹理；不伪装生长进度，也不需要持久化状态。
    // Tree rings are fixed ornament, not growth progress, and need no persistent state.
    for (int n = 0; n < 12; ++n) {
        float radius = 30 + n * 15;
        wfs_point_t previous = {0, 0};
        for (int i = 0; i <= 120; ++i) {
            float a = i * SCENE_TAU / 120.0f;
            float r = radius + 4 * sinf(a * 3 + n * 0.9f) + 3 * cosf(a * 7 + n * 0.7f);
            wfs_point_t current = polar(r, a, 231, 235);
            current.x = roundf(current.x * 100) / 100;
            current.y = roundf(current.y * 100) / 100;
            if (i > 0) {
                wfs_bar_opa(d, previous.x, previous.y, current.x, current.y,
                            n % 3 == 0 ? 1.4f : 1, n % 3 == 0 ? 0x9a9d72 : 0x737f5c,
                            n % 3 == 0 ? 0.72f : 0.65f);
            }
            previous = current;
        }
    }
    char title[24];
    snprintf(title, sizeof(title), "GROWTH / %04d", data->time.tm_year + 1900);
    wfs_text(d, title, 233, 76, 13, 0xd1cfb1, 3);
    for (int hour = 0; hour < 12; hour += 3) {
        wfs_point_t p = polar(192, hour * SCENE_TAU / 12.0f, 233, 233);
        wfs_disc(d, p.x, p.y, 3, 0xd3ceb0);
    }
    wfs_point_t hour = polar(94, ((data->time.tm_hour % 12) + data->time.tm_min / 60.0f) * SCENE_TAU / 12.0f, 233, 233);
    wfs_point_t minute = polar(151, (data->time.tm_min + data->time.tm_sec / 60.0f) * SCENE_TAU / 60.0f, 233, 233);
    wfs_point_t second = polar(172, data->time.tm_sec * SCENE_TAU / 60.0f, 233, 233);
    wfs_line(d, 233, 233, hour.x, hour.y, 7, 0xe9e5c9);
    wfs_line(d, 233, 233, minute.x, minute.y, 3.5f, 0xe9e5c9);
    wfs_bar(d, 233, 233, second.x, second.y, 1.5f, 0xda9c63);
    wfs_disc(d, 233, 233, 6, 0xd9d8be);
    wfs_disc(d, 233, 233, 2, 0x242820);
    wfs_rect(d, 171, 298, 124, 42, 21, 0x242820);
    char time[8];
    wfs_time(data, time, sizeof(time));
    wfs_text(d, time, 233, 329, 30, 0xdedbc0, 2);
    face_date(d, data, DATE_GROWTH, 407, 14, 0xb6b99a, 2);
}

void watchface_scenes_render(lv_layer_t *layer, const lv_area_t *area,
                             const watchface_data_t *data, int kind, bool preview)
{
    static const char *const names[] = {"MAZE", "FOLD", "PIXEL CAMP", "HORIZON", "TIDE", "GROWTH"};
    if (!data || !area || kind < 0 || kind >= 6) return;
    wfs_draw_t d = wfs_make(layer, area, preview);
    if (data->aod) {
        // 所有装饰在常显分支之前截断，同一分钟不会残留秒动画。
        // AOD exits before any decoration, so second animation never leaks into a fixed minute.
        wfs_aod(&d, data, names[kind]);
        return;
    }
    switch (kind) {
        case 0: maze(&d, data); break;
        case 1: fold(&d, data); break;
        case 2: pixel_camp(&d, data); break;
        case 3: horizon(&d, data); break;
        case 4: tide(&d, data); break;
        case 5: growth(&d, data); break;
    }
}
