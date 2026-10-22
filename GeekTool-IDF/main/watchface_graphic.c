#include "watchface_graphic.h"
#include "watchface_studio_draw.h"

#define GRAPHIC_TAU 6.2831853071795864769f

static wfs_point_t point(float x, float y, float radius, float turn)
{
    return (wfs_point_t){x + sinf(turn * GRAPHIC_TAU) * radius,
                         y - cosf(turn * GRAPHIC_TAU) * radius};
}

static void date_line(const wfs_draw_t *d, const watchface_data_t *data,
                      float y, uint32_t color)
{
    char date[32];
    wfs_date(data, date, sizeof date);
    wfs_text(d, date, 233, y, 14, color, 2);
}

static void sector(const wfs_draw_t *d, const watchface_data_t *data)
{
    const uint32_t colors[] = {0xb6d9ff, 0x609bfb, 0xd7ed78};
    const float progress[] = {((data->time.tm_hour % 12) + data->time.tm_min / 60.0f) / 12,
                              (data->time.tm_min + data->time.tm_sec / 60.0f) / 60,
                              data->time.tm_sec / 60.0f};
    // 三道弧分别表示12小时、分钟、秒，完整底轨避免零值被误读为缺失。
    // Three arcs encode 12-hour time, minutes and seconds; full tracks keep zero values legible.
    for (int row = 0; row < 3; ++row) {
        float radius = 204 - row * 24;
        wfs_arc(d, 233, 233, radius, -90, 180, 10, 0x14202d);
        wfs_arc(d, 233, 233, radius, -90, -90 + 270 * progress[row], 10, colors[row]);
        wfs_point_t tip = point(233, 233, radius, progress[row] * .75f);
        wfs_disc(d, tip.x, tip.y, 5, colors[row]);
    }
    wfs_text(d, "SECTOR", 233, 180, 14, 0x8facc7, 4);
    char time[8];wfs_time(data, time, sizeof time);
    wfs_heavy_text(d, time, 233, 270, 96, 0xecf4ff, 0);
    date_line(d, data, 311, 0x8facc7);
    wfs_text(d, "12 / 60 / 60", 145, 112, 12, 0xd7ed78, 1);
}

static void solar(const wfs_draw_t *d, const watchface_data_t *data)
{
    // 日轮是时间意象，不使用位置或天气推断真实日出日落。
    // The sun is a clock motif, not an inferred sunrise, sunset or weather reading.
    for (int ray = 0; ray < 36; ++ray) {
        float turn = ray / 36.0f;
        wfs_point_t a = point(233, 181, ray % 3 ? 91 : 83, turn);
        wfs_point_t b = point(233, 181, ray % 3 ? 119 : 133, turn);
        wfs_bar(d, a.x, a.y, b.x, b.y, ray % 3 ? 2 : 4, 0x8e4927);
    }
    wfs_disc(d, 233, 181, 67, 0xf4a85a);
    wfs_disc(d, 250, 167, 58, 0xffcf88);
    wfs_point_t second = point(233, 181, 133, data->time.tm_sec / 60.0f);
    wfs_disc(d, second.x, second.y, 5, 0xffe1ab);
    wfs_text(d, "S O L A R", 233, 43, 12, 0xdba06b, 2);
    char time[8];wfs_time(data, time, sizeof time);
    wfs_text(d, time, 233, 369, 99, 0xffe5c2, 1);
    date_line(d, data, 407, 0xc1936b);
}

static void grid(const wfs_draw_t *d, const watchface_data_t *data)
{
    // 固定18条细网格，不分配画布；扫描标记随真实秒值移动。
    // Eighteen fixed grid lines require no canvas; the scan marker follows real seconds.
    for (int n = 0; n < 9; ++n) {
        float p = 73 + n * 40;
        wfs_line(d, p, 35, p, 431, 1, 0x102b21);
        wfs_line(d, 35, p, 431, p, 1, 0x102b21);
    }
    wfs_text(d, "GRID / LOCAL TIME", 233, 83, 12, 0x7ca592, 2);
    char hour[4], minute[4], seconds[12];
    snprintf(hour, sizeof hour, "%02d", data->time.tm_hour);
    snprintf(minute, sizeof minute, "%02d", data->time.tm_min);
    wfs_heavy_text(d, hour, 170, 227, 146, 0xd1f6c4, 0);
    wfs_heavy_text(d, minute, 296, 346, 146, 0x71cda3, 0);
    float scan = 95 + data->time.tm_sec * 276.0f / 59;
    wfs_bar(d, 95, 373, 371, 373, 2, 0x25493b);
    wfs_rect(d, scan - 3, 368, 6, 11, 1, 0xd1f6c4);
    snprintf(seconds, sizeof seconds, "SEC / %02d", data->time.tm_sec);
    wfs_text(d, seconds, 233, 408, 12, 0x9ac9ab, 2);
}

static void petal(const wfs_draw_t *d, const watchface_data_t *data)
{
    const uint32_t colors[] = {0x7761ab, 0xb8a0e3, 0xe5c9f3, 0x947bc9};
    for (int n = 0; n < 4; ++n) {
        wfs_point_t p = point(233, 233, 94, n / 4.0f + .125f);
        wfs_disc(d, p.x, p.y, 100, colors[n]);
    }
    wfs_disc(d, 233, 233, 105, 0x000000);
    for (int n = 0; n < 12; ++n) {
        wfs_point_t p = point(233, 233, 91, n / 12.0f);
        wfs_disc(d, p.x, p.y, n % 3 ? 1.5f : 3, 0xb8a0e3);
    }
    wfs_point_t hour = point(233, 233, 49, ((data->time.tm_hour % 12) + data->time.tm_min / 60.0f) / 12);
    wfs_point_t minute = point(233, 233, 78, (data->time.tm_min + data->time.tm_sec / 60.0f) / 60);
    wfs_bar(d, 233, 233, hour.x, hour.y, 9, 0xf7e9ff);
    wfs_bar(d, 233, 233, minute.x, minute.y, 4, 0xf7e9ff);
    wfs_point_t second = point(233, 233, 97, data->time.tm_sec / 60.0f);
    wfs_disc(d, second.x, second.y, 4, 0xffbd78);
    wfs_disc(d, 233, 233, 6, 0xffbd78);
    wfs_text(d, "PETAL", 233, 44, 13, 0xd9c3ee, 5);
    date_line(d, data, 430, 0xb8a0e3);
}

static void ribbon(const wfs_draw_t *d, const watchface_data_t *data)
{
    char hour[4], minute[4];
    snprintf(hour, sizeof hour, "%02d", data->time.tm_hour);
    snprintf(minute, sizeof minute, "%02d", data->time.tm_min);
    wfs_text(d, "RIBBON / TIME IN COLOUR", 233, 71, 12, 0xd6a29a, 1);
    wfs_rect(d, 65, 105, 246, 137, 16, 0xf16d5c);
    wfs_rect(d, 145, 242, 246, 137, 16, 0xf1c5af);
    wfs_heavy_text(d, hour, 188, 227, 144, 0x231311, 0);
    wfs_heavy_text(d, minute, 268, 364, 144, 0x36231e, 0);
    // 六十个短标识编码秒；以零为起点，避免绘制60秒的虚假满进度。
    // Sixty short marks encode seconds from zero, without a false completed minute.
    for (int n = 0; n < 60; ++n) {
        wfs_point_t a = point(233, 233, 212, n / 60.0f);
        wfs_point_t b = point(233, 233, 217, n / 60.0f);
        wfs_bar(d, a.x, a.y, b.x, b.y, 2, n < data->time.tm_sec ? 0xf16d5c : 0x382421);
    }
    date_line(d, data, 409, 0xd6a29a);
}

static void binary(const wfs_draw_t *d, const watchface_data_t *data)
{
    const int values[] = {data->time.tm_hour, data->time.tm_min, data->time.tm_sec};
    const uint32_t colors[] = {0xe7e8d9, 0xe7b953, 0xc77842};
    const char *const labels[] = {"HOURS", "MINUTES", "SECONDS"};
    wfs_text(d, "BINARY / 24H", 233, 76, 14, 0xb4ae95, 3);
    // 左高位右低位，权值32..1；每行同时保留十进制读数。
    // Bits run from 32 to 1, left to right; decimal values keep every row readable.
    for (int row = 0; row < 3; ++row) {
        int y = 139 + row * 91;
        for (int bit = 0; bit < 6; ++bit)
            wfs_rect(d, 78 + bit * 44, y, 30, 33, 6,
                     values[row] & (1 << (5 - bit)) ? colors[row] : 0x25251f);
        char value[4];snprintf(value, sizeof value, "%02d", values[row]);
        wfs_text(d, value, 370, y + 30, 30, colors[row], 0);
        wfs_text(d, labels[row], 202, y + 55, 12, 0x9f9d88, 2);
    }
    date_line(d, data, 424, 0xb4ae95);
}

void watchface_graphic_render(lv_layer_t *layer, const lv_area_t *area,
                             const watchface_data_t *data, int kind, bool preview)
{
    static const char *const names[] = {"SECTOR", "SOLAR", "GRID", "PETAL", "RIBBON", "BINARY"};
    if (!data || !area || kind < 0 || kind >= 6) return;
    wfs_draw_t d = wfs_make(layer, area, preview);
    // 常显在装饰前返回；熄屏、唤醒和刷新仍由原控制器统一管理。
    // Exit before decoration in AOD; the existing controller owns sleep, wake and refresh.
    if (data->aod) {wfs_aod(&d, data, names[kind]);return;}
    switch (kind) {
        case 0: sector(&d, data); break;
        case 1: solar(&d, data); break;
        case 2: grid(&d, data); break;
        case 3: petal(&d, data); break;
        case 4: ribbon(&d, data); break;
        case 5: binary(&d, data); break;
    }
}
