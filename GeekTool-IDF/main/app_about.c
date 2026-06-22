// 关于本机 —— soRound OS:为圆形屏设计的极简手表系统。
// Nothing 单色 + 唯一红强调:同心点描环徽标(圆=round)+ 红核;字标 soRound(白)+ OS(红)。
#include "app.h"
#include "glyph.h"

#define AB_CX 233

static lv_obj_t *mklabel(lv_obj_t *p, const lv_font_t *f, uint32_t col, int y, const char *txt) {
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, txt);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    return l;
}

static void about_enter(lv_obj_t *parent) {
    // 同心点描环徽标(品牌:圆)+ 唯一红核
    int ey = 142;
    glyph_circle(parent, AB_CX, ey, 52, 13, 3, COL_TXT);
    glyph_circle(parent, AB_CX, ey, 36, 12, 3, COL_TXT);
    glyph_circle(parent, AB_CX, ey, 20, 11, 3, COL_TXT);
    glyph_dot(parent, AB_CX, ey, 6, COL_RED);

    // 字标:soRound(白) + OS(红),flex 行自动居中
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 220);
    lv_obj_t *n1 = lv_label_create(row);
    lv_obj_set_style_text_font(n1, UI_FONT_SYM, 0);
    lv_obj_set_style_text_color(n1, lv_color_hex(COL_TXT), 0);
    lv_label_set_text(n1, "soRound");
    lv_obj_t *n2 = lv_label_create(row);
    lv_obj_set_style_text_font(n2, UI_FONT_SYM, 0);
    lv_obj_set_style_text_color(n2, lv_color_hex(COL_RED), 0);
    lv_label_set_text(n2, "OS");

    mklabel(parent, &lv_font_montserrat_14, COL_TXT2, 252, "round-first watch os");

    glyph_line(parent, 112, 290, 354, 290, 12, 2, COL_TXT2);   // 点描分隔线

    mklabel(parent, UI_FONT_M, COL_TXT2, 304, "version 1.0");
    mklabel(parent, UI_FONT_M, COL_TXT2, 330, "ESP32-S3 / 466 round");
    mklabel(parent, &lv_font_montserrat_14, COL_TXT2, 360, "GeekTool");
}

const app_t app_about = { "about", COL_TXT, about_enter, NULL, NULL };
