/*
 * GeekTool —— ESP32-S3-Touch-AMOLED-1.75C 多功能仪表平台
 * ------------------------------------------------------------
 * 径向启动器 + 可插拔 App 框架。每个工具 = 一个 .cpp(实现 enter/tick/exit)
 * + 在下面 APPS[] 注册一行。
 *
 * 已含 app:WiFi(扫描+连接)、I2C 扫描、系统信息。
 * 导航:点启动器圆点进入工具;在工具里【向右滑】或点左上角【‹】返回。
 *
 * 依赖:lvgl 8.4.0 / GFX Library for Arduino 1.6.4 / SensorLib 0.3.3
 *       esp32 core 3.3.5(见 README)
 */
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <lvgl.h>
#include "Arduino_GFX_Library.h"
#include "TouchDrvCSTXXX.hpp"
#include "pin_config.h"
#include "esp_timer.h"
#include "app.h"

/* ---------- HAL 全局(app.h 中 extern) ---------- */
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *gfx = new Arduino_CO5300(
    bus, LCD_RESET, 0, LCD_WIDTH, LCD_HEIGHT, 6, 0, 0, 0);
TouchDrvCST92xx touch;
uint32_t screenW, screenH;

/* ---------- App 注册表 ---------- */
const app_t *const APPS[] = { &app_wifi, &app_i2c, &app_sys };
const int APP_COUNT = sizeof(APPS) / sizeof(APPS[0]);

/* ---------- 内部状态 ---------- */
#define TICK_MS 2
static lv_disp_draw_buf_t draw_buf;
static int16_t tp_x[5], tp_y[5];

static lv_obj_t *launcher_screen = NULL;
static lv_obj_t *app_screen = NULL;
static const app_t *cur_app = NULL;

static lv_obj_t *g_title = NULL;   // 顶部标题(top layer)
static lv_obj_t *g_back  = NULL;   // 返回按钮(top layer)

/* ============ LVGL HAL 回调 ============ */
static void disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *px) {
  uint32_t w = area->x2 - area->x1 + 1, h = area->y2 - area->y1 + 1;
#if (LV_COLOR_16_SWAP != 0)
  gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)px, w, h);
#else
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px, w, h);
#endif
  lv_disp_flush_ready(disp);
}
static void disp_rounder(lv_disp_drv_t *d, lv_area_t *a) {
  if (a->x1 % 2) a->x1--;
  if (a->y1 % 2) a->y1--;
  if (a->x2 % 2 == 0) a->x2++;
  if (a->y2 % 2 == 0) a->y2++;
}
static void touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  uint8_t n = touch.getPoint(tp_x, tp_y, 1);
  if (n > 0) { data->state = LV_INDEV_STATE_PR; data->point.x = tp_x[0]; data->point.y = tp_y[0]; }
  else        data->state = LV_INDEV_STATE_REL;
}
static void lv_tick_cb(void *arg) { lv_tick_inc(TICK_MS); }

/* ============ 导航 ============ */
static void gesture_cb(lv_event_t *e) {
  if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) go_home();
}
static void back_cb(lv_event_t *e) { go_home(); }

static void open_app(int idx) {
  if (idx < 0 || idx >= APP_COUNT) return;
  cur_app = APPS[idx];

  app_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(app_screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(app_screen, LV_OPA_COVER, 0);
  lv_obj_clear_flag(app_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(app_screen, gesture_cb, LV_EVENT_GESTURE, NULL);

  if (cur_app->enter) cur_app->enter(app_screen);

  lv_label_set_text(g_title, cur_app->name);
  lv_obj_clear_flag(g_title, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(g_back, LV_OBJ_FLAG_HIDDEN);

  lv_scr_load(app_screen);
}

void go_home(void) {
  if (!cur_app) return;
  lv_obj_add_flag(g_title, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_back, LV_OBJ_FLAG_HIDDEN);
  if (cur_app->exit) cur_app->exit();
  cur_app = NULL;
  lv_scr_load(launcher_screen);                 // 先切回启动器
  /* 延迟删除旧屏:此函数可能由旧屏自身的手势事件触发,直接删会用后即焚崩溃 */
  if (app_screen) { lv_obj_del_async(app_screen); app_screen = NULL; }
}

static void dot_cb(lv_event_t *e) {
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  open_app(idx);
}

/* ============ 顶层悬浮:电量环 + 标题 + 返回 ============ */
static void build_overlay(void) {
  lv_obj_t *top = lv_layer_top();

  lv_obj_t *batt = lv_arc_create(top);
  lv_obj_set_size(batt, 458, 458);
  lv_obj_center(batt);
  lv_arc_set_rotation(batt, 270);
  lv_arc_set_bg_angles(batt, 0, 360);
  lv_arc_set_range(batt, 0, 100);
  lv_arc_set_value(batt, 72);                    // TODO: 接 AXP2101 真实电量
  lv_obj_remove_style(batt, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(batt, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_color(batt, lv_color_hex(0x15151a), LV_PART_MAIN);
  lv_obj_set_style_arc_width(batt, 8, LV_PART_MAIN);
  lv_obj_set_style_arc_color(batt, lv_color_hex(COL_RING), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(batt, 8, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(batt, true, LV_PART_INDICATOR);

  g_title = lv_label_create(top);
  lv_label_set_text(g_title, "");
  lv_obj_set_style_text_color(g_title, lv_color_hex(COL_TXT), 0);
  lv_obj_set_style_text_font(g_title, &lv_font_montserrat_14, 0);
  lv_obj_align(g_title, LV_ALIGN_TOP_MID, 0, 46);
  lv_obj_add_flag(g_title, LV_OBJ_FLAG_HIDDEN);

  g_back = lv_btn_create(top);
  lv_obj_set_size(g_back, 46, 34);
  lv_obj_set_style_radius(g_back, 17, 0);
  lv_obj_set_style_bg_color(g_back, lv_color_hex(0x26262c), 0);
  lv_obj_align(g_back, LV_ALIGN_TOP_MID, -96, 42);
  lv_obj_add_event_cb(g_back, back_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *bl = lv_label_create(g_back);
  lv_label_set_text(bl, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_color(bl, lv_color_white(), 0);
  lv_obj_center(bl);
  lv_obj_add_flag(g_back, LV_OBJ_FLAG_HIDDEN);
}

/* ============ 横向轮播启动器(一次一个大图标 + 两侧黑渐隐 + 箭头) ============ */
#define PAGEW LCD_WIDTH   // 整屏一页 → 一次只居中显示一个 app
#define ICON  196         // 大图标直径

static lv_obj_t *g_arrow_l = NULL, *g_arrow_r = NULL;

/* 仅在到头/到尾时隐藏对应箭头;不改尺寸/透明度,所以很轻、很顺 */
static void carousel_scroll_cb(lv_event_t *e) {
  lv_obj_t *car = lv_event_get_target(e);
  lv_coord_t sx = lv_obj_get_scroll_x(car);
  lv_coord_t maxs = (APP_COUNT - 1) * PAGEW;
  if (g_arrow_l) { if (sx > 12)        lv_obj_clear_flag(g_arrow_l, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g_arrow_l, LV_OBJ_FLAG_HIDDEN); }
  if (g_arrow_r) { if (sx < maxs - 12) lv_obj_clear_flag(g_arrow_r, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g_arrow_r, LV_OBJ_FLAG_HIDDEN); }
}

static void arrow_l_cb(lv_event_t *e) { lv_obj_scroll_by((lv_obj_t *)lv_event_get_user_data(e),  PAGEW, 0, LV_ANIM_ON); }
static void arrow_r_cb(lv_event_t *e) { lv_obj_scroll_by((lv_obj_t *)lv_event_get_user_data(e), -PAGEW, 0, LV_ANIM_ON); }

/* 已移除两侧渐隐遮罩:多档透明黑条会让滑动中的亮图标出现分档/割裂感,
   且每帧都要在移动内容上做半透明混合,很费性能。整屏一页本身已保证
   "一次只显示一个图标";若以后想要柔和黑边,改用一张带 alpha 的图更平滑。 */

static lv_obj_t *make_arrow(lv_obj_t *parent, const char *sym, bool left, lv_obj_t *car) {
  lv_obj_t *a = lv_label_create(parent);
  lv_label_set_text(a, sym);
  lv_obj_set_style_text_color(a, lv_color_hex(COL_TXT2), 0);
  lv_obj_set_style_text_font(a, &lv_font_montserrat_20, 0);
  lv_obj_align(a, left ? LV_ALIGN_LEFT_MID : LV_ALIGN_RIGHT_MID, left ? 12 : -12, 0);
  lv_obj_add_flag(a, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(a, 24);
  lv_obj_add_event_cb(a, left ? arrow_l_cb : arrow_r_cb, LV_EVENT_CLICKED, car);
  return a;
}

static void build_launcher(void) {
  launcher_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(launcher_screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(launcher_screen, LV_OPA_COVER, 0);
  lv_obj_clear_flag(launcher_screen, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *car = lv_obj_create(launcher_screen);
  lv_obj_set_size(car, LCD_WIDTH, LCD_HEIGHT);
  lv_obj_center(car);
  lv_obj_set_style_bg_opa(car, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(car, 0, 0);
  lv_obj_set_flex_flow(car, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(car, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(car, 0, 0);
  lv_obj_set_style_pad_column(car, 0, 0);
  lv_obj_set_scroll_dir(car, LV_DIR_HOR);
  lv_obj_set_scroll_snap_x(car, LV_SCROLL_SNAP_CENTER);
  lv_obj_set_scrollbar_mode(car, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_event_cb(car, carousel_scroll_cb, LV_EVENT_SCROLL, NULL);

  for (int i = 0; i < APP_COUNT; i++) {
    lv_obj_t *page = lv_obj_create(car);
    lv_obj_set_size(page, PAGEW, LCD_HEIGHT);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon = lv_btn_create(page);
    lv_obj_set_size(icon, ICON, ICON);
    lv_obj_set_style_radius(icon, ICON / 2, 0);
    lv_obj_set_style_bg_color(icon, lv_color_hex(APPS[i]->color), 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -16);
    lv_obj_add_event_cb(icon, dot_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

    char letter[2] = { APPS[i]->name[0], 0 };
    lv_obj_t *gl = lv_label_create(icon);
    lv_label_set_text(gl, letter);
    lv_obj_set_style_text_color(gl, lv_color_black(), 0);
    lv_obj_set_style_text_font(gl, &lv_font_montserrat_20, 0);
    lv_obj_center(gl);

    lv_obj_t *name = lv_label_create(page);
    lv_label_set_text(name, APPS[i]->name);
    lv_obj_set_style_text_color(name, lv_color_hex(COL_TXT), 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_20, 0);
    lv_obj_align(name, LV_ALIGN_CENTER, 0, ICON / 2 + 14);
  }

  g_arrow_l = make_arrow(launcher_screen, LV_SYMBOL_LEFT, true, car);
  g_arrow_r = make_arrow(launcher_screen, LV_SYMBOL_RIGHT, false, car);

  lv_obj_update_layout(car);
  lv_event_send(car, LV_EVENT_SCROLL, NULL);   // 初始化箭头显隐
}

void setup() {
  Serial.begin(115200);

  /* I2C + 触摸 */
  Wire.begin(IIC_SDA, IIC_SCL);
  pinMode(TP_RST, OUTPUT);
  digitalWrite(TP_RST, LOW);  delay(30);
  digitalWrite(TP_RST, HIGH); delay(50);
  delay(200);
  touch.setPins(TP_RST, TP_INT);
  if (!touch.begin(Wire, 0x5A, IIC_SDA, IIC_SCL)) Serial.println("CST9217 not found!");
  touch.setMaxCoordinates(466, 466);
  touch.setMirrorXY(true, true);

  /* 显示 */
  gfx->begin();
  gfx->setBrightness(200);
  screenW = gfx->width();
  screenH = gfx->height();

  /* LVGL */
  lv_init();
  /* 全屏单缓冲(PSRAM):整帧一次写出,把"分 4 块、上块先到下块后到"的台阶式刷新
     合成一次连续刷新(代价:渲染走 PSRAM,略慢) */
  uint32_t bufPx = screenW * screenH;
  lv_color_t *b1 = (lv_color_t *)heap_caps_malloc(bufPx * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  if (!b1) b1 = (lv_color_t *)heap_caps_malloc(bufPx * sizeof(lv_color_t), MALLOC_CAP_DMA);
  lv_disp_draw_buf_init(&draw_buf, b1, NULL, bufPx);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = screenW;
  disp_drv.ver_res = screenH;
  disp_drv.flush_cb = disp_flush;
  disp_drv.rounder_cb = disp_rounder;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touch_read;
  lv_indev_drv_register(&indev_drv);

  const esp_timer_create_args_t targ = { .callback = &lv_tick_cb, .name = "lv_tick" };
  esp_timer_handle_t th; esp_timer_create(&targ, &th);
  esp_timer_start_periodic(th, TICK_MS * 1000);

  build_overlay();
  build_launcher();
  lv_scr_load(launcher_screen);

  Serial.println("GeekTool ready");
}

void loop() {
  lv_timer_handler();
  if (cur_app && cur_app->tick) cur_app->tick();
  delay(2);
}
