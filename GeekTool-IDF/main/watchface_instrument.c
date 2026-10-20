#include "watchface_instrument.h"
#include "watchface_studio_draw.h"
#include <math.h>
#include <stdio.h>

#define WFI_PI 3.14159265358979323846f

static wfs_point_t polar(float cx, float cy, float radius, float angle) {
    return (wfs_point_t){roundf((cx + cosf(angle) * radius) * 100) / 100,
                         roundf((cy + sinf(angle) * radius) * 100) / 100};
}

static void radial_bar(const wfs_draw_t *d, float radius1, float radius2,
                       float angle, float width, uint32_t color) {
    wfs_point_t a = polar(233, 233, radius1, angle);
    wfs_point_t b = polar(233, 233, radius2, angle);
    wfs_bar(d, a.x, a.y, b.x, b.y, width, color);
}

static void subdued_time(const wfs_draw_t *d, const watchface_data_t *data, float baseline) {
    char time[8];
    wfs_time(data, time, sizeof time);
    wfs_disc(d, 233, 233, 233, 0x070908);
    wfs_text(d, time, 233, baseline, 72, 0x4b4d4b, -2);
}

static void instrument_date(const watchface_data_t *data, char *buffer, size_t size) {
    static const char *const days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    snprintf(buffer, size, "%s   %02d / %02d", days[LV_CLAMP(0, data->time.tm_wday, 6)],
             data->time.tm_mon + 1, data->time.tm_mday);
}

static void radar(const wfs_draw_t *d, const watchface_data_t *data) {
    if (data->aod) {
        subdued_time(d, data, 262);
        for (int i = 0; i < 4; ++i) radial_bar(d, 180, 190, i * WFI_PI / 2, 1, 0x202824);
        return;
    }

    char time[8], date[24], seconds[12];
    wfs_time(data, time, sizeof time);
    instrument_date(data, date, sizeof date);
    snprintf(seconds, sizeof seconds, "%02d SEC", data->time.tm_sec);
    wfs_disc(d, 233, 233, 233, 0x08130f);

    float sweep = data->time.tm_sec * WFI_PI / 30 - WFI_PI / 2;
    for (int offset = -160; offset <= 160; offset += 40) {
        float reach = sqrtf(196.0f * 196 - offset * offset);
        wfs_bar(d, 233 + offset, 233 - reach, 233 + offset, 233 + reach, 1, 0x142b22);
        wfs_bar(d, 233 - reach, 233 + offset, 233 + reach, 233 + offset, 1, 0x142b22);
    }
    static const int radii[] = {64, 128, 196};
    for (unsigned i = 0; i < sizeof radii / sizeof radii[0]; ++i)
        wfs_circle(d, 233, 233, radii[i], 1, 0x1d3e2e);
    for (int i = 0; i < 60; ++i) {
        bool major = i % 5 == 0;
        radial_bar(d, major ? 187 : 193, 196, i * WFI_PI / 30 - WFI_PI / 2,
                   major ? 1.5f : 1, major ? 0x4f765b : 0x274531);
    }
    for (int i = 0; i < 12; ++i) {
        float start = sweep - (i + 1) * WFI_PI / 90;
        wfs_point_t sector[10] = {{233, 233}};
        for (int step = 0; step <= 8; ++step)
            sector[step + 1] = polar(233, 233, 195, start + step * WFI_PI / 720);
        // 按原图层顺序半透明叠加，保留余辉下面的网格。 / Blend above the grid in the original SVG layer order.
        wfs_poly_opa(d, sector, 10, 0x89dc91, (0.045f - i * 0.003f));
    }

    // 秒扫线只表示真实时钟，不添加虚构目标点。 / The sweep represents real seconds, not invented targets.
    wfs_point_t tip = polar(233, 233, 195, sweep);
    wfs_bar(d, 233, 233, tip.x, tip.y, 1.4f, 0x9ce7a1);
    wfs_disc(d, 233, 233, 3, 0xa5e7aa);
    wfs_text(d, "RADAR", 233, 110, 13, 0x7baf88, 6);
    wfs_rect_opa(d, 73, 181, 320, 102, 16, 0x08130f, 0.91f);
    wfs_text(d, time, 233, 261, 91, 0xd3f3c9, -2.5f);
    wfs_text(d, date, 233, 327, 13, 0x78977d, 2);
    wfs_text(d, seconds, 233, 366, 10, 0x52735b, 3);
}

static wfs_point_t rotate_local(wfs_point_t point, float cx, float cy, float angle) {
    float sine = sinf(angle), cosine = cosf(angle);
    return (wfs_point_t){cx + point.x * cosine - point.y * sine,
                         cy + point.x * sine + point.y * cosine};
}

static wfs_point_t cubic_point(wfs_point_t a, wfs_point_t b, wfs_point_t c,
                              wfs_point_t e, float t) {
    float u = 1 - t;
    return (wfs_point_t){u * u * u * a.x + 3 * u * u * t * b.x + 3 * u * t * t * c.x + t * t * t * e.x,
                         u * u * u * a.y + 3 * u * u * t * b.y + 3 * u * t * t * c.y + t * t * t * e.y};
}

static void reel_curve(const wfs_draw_t *d, float cx, float cy, float angle,
                       wfs_point_t a, wfs_point_t b, wfs_point_t c, wfs_point_t e) {
    // 原稿三次曲线固定分成32段，保留控制点与卷盘相位。 / Preserve cubic control points and reel phase with 32 segments.
    wfs_point_t previous = rotate_local(a, cx, cy, angle);
    for (int i = 1; i <= 32; ++i) {
        wfs_point_t next = rotate_local(cubic_point(a, b, c, e, i / 32.0f), cx, cy, angle);
        wfs_bar(d, previous.x, previous.y, next.x, next.y, 1.5f, 0xc2bba5);
        previous = next;
    }
}

static void reel(const wfs_draw_t *d, float cx, float cy, float rotation) {
    wfs_disc(d, cx, cy, 65, 0x12120f);
    wfs_circle(d, cx, cy, 65, 1.25f, 0x7b776a);
    wfs_circle(d, cx, cy, 58, 0.7f, 0x3f3d34);
    for (int spoke = 0; spoke < 3; ++spoke) {
        float angle = rotation + spoke * WFI_PI * 2 / 3;
        reel_curve(d, cx, cy, angle, (wfs_point_t){-6, -17}, (wfs_point_t){-11, -29},
                   (wfs_point_t){-9, -49}, (wfs_point_t){0, -51});
        reel_curve(d, cx, cy, angle, (wfs_point_t){0, -51}, (wfs_point_t){14, -51},
                   (wfs_point_t){23, -38}, (wfs_point_t){14, -24});
        wfs_point_t a = rotate_local((wfs_point_t){14, -24}, cx, cy, angle);
        wfs_point_t b = rotate_local((wfs_point_t){7, -13}, cx, cy, angle);
        wfs_bar(d, a.x, a.y, b.x, b.y, 1.5f, 0xc2bba5);
        // 仅完整路径两端圆帽，内部曲线分段保持平端。 / Round only the complete path ends, not each curve segment.
        wfs_point_t first = rotate_local((wfs_point_t){-6, -17}, cx, cy, angle);
        wfs_disc(d, first.x, first.y, 0.75f, 0xc2bba5);
        wfs_disc(d, b.x, b.y, 0.75f, 0xc2bba5);
    }
    wfs_circle(d, cx, cy, 16, 1.25f, 0xc2bba5);
    wfs_point_t a = rotate_local((wfs_point_t){-5, 0}, cx, cy, rotation);
    wfs_point_t b = rotate_local((wfs_point_t){5, 0}, cx, cy, rotation);
    wfs_point_t c = rotate_local((wfs_point_t){0, -5}, cx, cy, rotation);
    wfs_point_t e = rotate_local((wfs_point_t){0, 5}, cx, cy, rotation);
    wfs_bar(d, a.x, a.y, b.x, b.y, 1.1f, 0xc2bba5);
    wfs_bar(d, c.x, c.y, e.x, e.y, 1.1f, 0xc2bba5);
}

static void tape(const wfs_draw_t *d, const watchface_data_t *data) {
    if (data->aod) {
        subdued_time(d, data, 303);
        wfs_circle(d, 156, 165, 16, 1, 0x252522);
        wfs_circle(d, 310, 165, 16, 1, 0x252522);
        return;
    }

    char time[8], date[24];
    wfs_time(data, time, sizeof time);
    instrument_date(data, date, sizeof date);
    wfs_disc(d, 233, 233, 233, 0x13130f);
    wfs_text(d, date, 233, 67, 12, 0x99957f, 2);
    wfs_bar(d, 156, 100, 310, 100, 1, 0x6d6755);
    wfs_bar(d, 156, 230, 310, 230, 1, 0x6d6755);
    // 右卷盘采用四分之三速；分钟参与相位，整分钟也不会倒跳。 / The right reel uses three-quarter speed with continuous minute phase.
    float rotation = (data->time.tm_sec + data->time.tm_min * 60) * WFI_PI / 30;
    reel(d, 156, 165, rotation);
    reel(d, 310, 165, -rotation * 0.75f);
    wfs_bar(d, 110, 211, 149, 246, 1, 0x8e866d);
    wfs_bar(d, 149, 246, 317, 246, 1, 0x8e866d);
    wfs_bar(d, 317, 246, 356, 211, 1, 0x8e866d);
    wfs_bar(d, 163, 244, 163, 250, 2, 0xc0b699);
    wfs_bar(d, 303, 244, 303, 250, 2, 0xc0b699);
    wfs_text(d, time, 233, 326, 87, 0xf1ead6, -2);
    wfs_bar(d, 170, 351, 296, 351, 1, 0x39392e);
    wfs_text(d, "T A P E", 233, 378, 12, 0xa49e88, 3);
    wfs_text(d, "TIME IN MOTION", 233, 400, 8, 0x646450, 2.6f);
}

static wfs_point_t rotate_blade(wfs_point_t point, float angle) {
    point.x -= 233;
    point.y -= 233;
    return rotate_local(point, 233, 233, angle);
}

static void iris_blade(const wfs_draw_t *d, int index) {
    wfs_point_t points[36];
    float angle = index * WFI_PI / 4;
    for (int i = 0; i <= 32; ++i) {
        wfs_point_t p = cubic_point((wfs_point_t){233, 35}, (wfs_point_t){296, 35},
                                    (wfs_point_t){350, 65}, (wfs_point_t){382, 108}, i / 32.0f);
        points[i] = rotate_blade(p, angle);
    }
    points[33] = rotate_blade((wfs_point_t){340, 191}, angle);
    points[34] = rotate_blade((wfs_point_t){307, 140}, angle);
    points[35] = rotate_blade((wfs_point_t){159, 140}, angle);
    wfs_poly(d, points, 36, index % 2 == 0 ? 0x24211c : 0x1b1a17);
    for (int i = 0; i < 36; ++i) {
        wfs_point_t a = points[i], b = points[(i + 1) % 36];
        wfs_bar(d, a.x, a.y, b.x, b.y, 1, 0x353027);
    }
    for (int i = 32; i < 34; ++i) {
        wfs_point_t a = points[i], b = points[i + 1];
        wfs_bar(d, a.x, a.y, b.x, b.y, 0.8f, 0x766044);
    }
}

static void iris(const wfs_draw_t *d, const watchface_data_t *data) {
    static const wfs_point_t aperture[] = {
        {159, 140}, {307, 140}, {340, 191}, {340, 275},
        {307, 326}, {159, 326}, {126, 275}, {126, 191}
    };
    if (data->aod) {
        subdued_time(d, data, 262);
        static const wfs_point_t upper[] = {{126, 191}, {159, 140}, {307, 140}, {340, 191}};
        static const wfs_point_t lower[] = {{340, 275}, {307, 326}, {159, 326}, {126, 275}};
        for (int i = 0; i < 3; ++i) {
            wfs_bar(d, upper[i].x, upper[i].y, upper[i + 1].x, upper[i + 1].y, 1, 0x292724);
            wfs_bar(d, lower[i].x, lower[i].y, lower[i + 1].x, lower[i + 1].y, 1, 0x292724);
        }
        return;
    }

    char time[8], date[24];
    wfs_time(data, time, sizeof time);
    instrument_date(data, date, sizeof date);
    wfs_disc(d, 233, 233, 233, 0x13120f);
    for (int i = 0; i < 8; ++i) iris_blade(d, i);
    wfs_circle(d, 233, 233, 211, 1, 0x4c3b28);
    for (int i = 0; i < 60; ++i)
        radial_bar(d, i % 5 == 0 ? 195 : 200, 204, i * WFI_PI / 30 - WFI_PI / 2,
                   0.8f, i % 5 == 0 ? 0x92704c : 0x4f4232);
    wfs_poly(d, aperture, 8, 0x11110f);
    for (int i = 0; i < 8; ++i) {
        wfs_point_t a = aperture[i], b = aperture[(i + 1) % 8];
        wfs_bar(d, a.x, a.y, b.x, b.y, 1.2f, 0xaf7a42);
    }
    wfs_text(d, "IRIS", 233, 189, 11, 0xb18b5d, 5);
    wfs_text(d, time, 233, 260, 72, 0xeee5d5, -2);
    wfs_text(d, date, 233, 296, 10, 0x9c8d76, 1.5f);
    wfs_point_t second = polar(233, 233, 204, data->time.tm_sec * WFI_PI / 30 - WFI_PI / 2);
    wfs_disc(d, second.x, second.y, 2.8f, 0xe7a65b);
    wfs_text(d, "APERTURE OF TIME", 233, 385, 9, 0x7c684e, 2.6f);
}

void watchface_instrument_render(lv_layer_t *layer, const lv_area_t *area,
                                const watchface_data_t *data, int kind, bool preview) {
    wfs_draw_t draw = wfs_make(layer, area, preview);
    switch (kind) {
        case 1: tape(&draw, data); break;
        case 2: iris(&draw, data); break;
        default: radar(&draw, data); break;
    }
}
