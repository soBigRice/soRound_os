// 日历 app —— 月视图网格(周一起始)。今天 = 唯一红色实心圆(满屏点描/描边里唯一的实心块,做视觉焦点)。
// 时间取本机 localtime(WiFi+SNTP 校时,TZ=CST-8);无网络也能画(按本机时间)。标题显示 "Month Year"。
// 静态一帧、无 tick:打开即画好,日期不在查看期间滚动,省得整屏重绘(天然避撕裂)。
#include "app.h"
#include "glyph.h"
#include <time.h>
#include <stdio.h>

// ---- 网格几何(466×466 圆屏)----
// 7 列居中:列心 x = COL0 + COLW*c → 89..377(总宽 336),四角都落在圆内不被裁。
// 行块按"本月周数"竖直居中(短月不空一截),表头/分隔线随行块上移。
#define CAL_COLW   48
#define CAL_ROWH   44
#define CAL_COL0   89
#define CAL_CENTER 256          // 行块竖直锚点(略低于屏心,给上方表头/标题让位)
#define CAL_DISC   38           // 今天红圆直径

static const char *const WD[7]  = { "M", "T", "W", "T", "F", "S", "S" };   // 周一起始
static const char *const MON[12] = { "January", "February", "March", "April", "May", "June",
                                     "July", "August", "September", "October", "November", "December" };

static int days_in_month(int year, int mon) {            // mon: 0-11
    static const int d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (mon == 1 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) return 29;
    return d[mon];
}

// 居中文字标签(在格心 cx,cy);手势冒泡到 app 屏,保证日期格上起手也能右滑返回
static lv_obj_t *cal_label(lv_obj_t *p, int cx, int cy, const char *txt, uint32_t col) {
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, UI_FONT_M, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, CAL_COLW);
    lv_label_set_text(l, txt);
    lv_obj_set_pos(l, cx - CAL_COLW / 2, cy - 8);
    lv_obj_add_flag(l, LV_OBJ_FLAG_EVENT_BUBBLE);
    return l;
}

static void calendar_enter(lv_obj_t *parent) {
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    int year = tm.tm_year + 1900, mon = tm.tm_mon, today = tm.tm_mday;

    char title[24];
    snprintf(title, sizeof title, "%s %d", MON[mon], year);
    launcher_set_title(title);

    int dim = days_in_month(year, mon);
    // 本月 1 号的星期(0=周日);周一起始下 1 号前的空格数
    int wday_first = ((tm.tm_wday - (today - 1)) % 7 + 7) % 7;
    int lead = (wday_first + 6) % 7;
    int nweeks = (lead + dim + 6) / 7;

    int row0_y = CAL_CENTER - (nweeks - 1) * CAL_ROWH / 2;   // 行块竖直居中
    int hdr_y  = row0_y - 52;
    int div_y  = row0_y - 26;

    // 周几表头(灰)
    for (int c = 0; c < 7; c++)
        cal_label(parent, CAL_COL0 + CAL_COLW * c, hdr_y, WD[c], COL_TXT2);

    // 表头下的点描分隔线(灰,与全局点描风一致)
    glyph_line(parent, CAL_COL0 - 24, div_y, CAL_COL0 + CAL_COLW * 6 + 24, div_y, 12, 2, COL_TXT2);

    // 日期格:今天先铺红色实心圆,再放白字;其余白字;非本月留空
    for (int d = 1; d <= dim; d++) {
        int cell = lead + (d - 1);
        int cx = CAL_COL0 + CAL_COLW * (cell % 7);
        int cy = row0_y + CAL_ROWH * (cell / 7);

        if (d == today) {
            lv_obj_t *disc = lv_obj_create(parent);
            lv_obj_remove_style_all(disc);
            lv_obj_set_size(disc, CAL_DISC, CAL_DISC);
            lv_obj_set_pos(disc, cx - CAL_DISC / 2, cy - CAL_DISC / 2);
            lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(disc, lv_color_hex(COL_RED), 0);
            lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
            lv_obj_remove_flag(disc, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(disc, LV_OBJ_FLAG_EVENT_BUBBLE);
        }
        char s[4];
        snprintf(s, sizeof s, "%d", d);
        cal_label(parent, cx, cy, s, COL_TXT);
    }
}

const app_t app_calendar = { "calendar", COL_TXT, calendar_enter, NULL, NULL };
