/* Offline website fixtures. Link real app controllers; substitute only device services.
 * No BLE, I2C, audio worker, network, or physical device is opened by this executable. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "app.h"
#include "ble_twin.h"
#include "power.h"
#include "board_config.h"
#include "freertos/task.h"
#include "../../GeekTool-IDF/tests/host/tools_render.h"

int64_t host_time_us = 1000000;
int64_t esp_timer_get_time(void) { return host_time_us; }
static uint32_t random_state = 0x12345678;
static lv_obj_t *heading;
static uint16_t pixels[466 * 466];
_Alignas(LV_DRAW_BUF_ALIGN) static uint16_t buffer[466 * 40];

uint8_t settings_lang(void) { return 1; }
time_t site_fixture_time(time_t *out) {
    time_t value = 1791338880; /* 2026-10-07 10:08 CST. */
    if (out) *out = value;
    return value;
}
uint32_t esp_random(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}
void launcher_set_title(const char *title) { lv_label_set_text(heading, title); }
bool launcher_app_visible(void) { return true; }
void buttons_reset_control(void) {}
bool buttons_control_pressed(void) { return false; }
void audio_out_init(void) {}
void audio_out_deinit(void) {}
void audio_out_alarm(void) {}
bool imu_init(void) { return true; }
bool imu_read_tilt(float *x, float *y) { *x = .10f; *y = .35f; return true; }
bool imu_read_accel(float *x, float *y, float *z) { *x = .10f; *y = .35f; *z = .93f; return true; }
bool imu_read_gyro(float *x, float *y, float *z) { *x = *y = *z = 0; return true; }
bool power_read(int *soc, pwr_state_t *state) { *soc = 74; *state = PWR_DISCHARGING; return true; }
bool wifi_service_enabled(void) { return false; }
void wifi_service_set_enabled(bool value) { (void)value; }
bool ble_twin_start(void) { return true; }
void ble_twin_stop(void) {}
bool ble_twin_connected(void) { return false; }
bool ble_twin_notify(const uint8_t *data, size_t size) { (void)data; (void)size; return false; }
void ble_twin_set_rx_cb(void (*cb)(const uint8_t *, size_t)) { (void)cb; }
i2c_master_bus_handle_t board_i2c_bus(void) { return NULL; }
esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t address, int timeout) {
    (void)bus; (void)timeout;
    return address == 0x18 || address == 0x34 || address == 0x40 || address == 0x51 ||
           address == 0x5a || address == 0x6b ? ESP_OK : ESP_FAIL;
}
int xTaskCreate(void (*task)(void *), const char *name, unsigned stack, void *arg,
                unsigned priority, void *handle) {
    (void)stack; (void)priority;
    if (handle) *(TaskHandle_t *)handle = (void *)1;
    /* The I2C worker terminates after a single fixture scan. Never run Twin's sampling loop. */
    if (strcmp(name, "i2c_scan") == 0) task(arg);
    return pdPASS;
}
void vTaskDelete(void *task) { (void)task; }
void vTaskDelay(int duration) { (void)duration; }
TickType_t xTaskGetTickCount(void) { return (TickType_t)(host_time_us / 1000); }
void vTaskDelayUntil(TickType_t *wake, TickType_t period) { *wake += period; }

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *map) {
    int width = lv_area_get_width(area);
    for (int y = area->y1; y <= area->y2; ++y) {
        memcpy(pixels + y * 466 + area->x1, map, (size_t)width * 2);
        map += width * 2;
    }
    lv_display_flush_ready(display);
}
static void capture(const char *directory, const char *name, lv_display_t *display) {
    lv_obj_update_layout(lv_screen_active());
    lv_refr_now(display);
    char path[1024];
    int length = snprintf(path, sizeof path, "%s/%s.ppm", directory, name);
    assert(length > 0 && (size_t)length < sizeof path);
    FILE *file = fopen(path, "wb"); assert(file);
    fprintf(file, "P6\n466 466\n255\n");
    for (unsigned i = 0; i < 466 * 466; ++i) {
        uint16_t p = pixels[i];
        uint8_t rgb[] = {((p >> 11) & 31) * 255 / 31, ((p >> 5) & 63) * 255 / 63, (p & 31) * 255 / 31};
        assert(fwrite(rgb, 1, 3, file) == 3);
    }
    assert(fclose(file) == 0);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    setenv("TZ", "CST-8", 1); tzset();
    const app_t *apps[] = {&app_calendar, &app_countdown, &app_stopwatch, &app_maze,
                          &app_fluid, &app_i2c, &app_twin};
    const char *names[] = {"calendar", "countdown", "stopwatch", "maze", "fluid", "i2c", "twin"};
    for (unsigned i = 0; i < sizeof apps / sizeof apps[0]; ++i) {
        lv_init(); i18n_init();
        lv_display_t *display = lv_display_create(466, 466);
        lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
        lv_display_set_buffers(display, buffer, NULL, sizeof buffer, LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_set_flush_cb(display, flush);
        lv_theme_t *theme = lv_theme_default_init(display, lv_color_hex(COL_RED),
                                                  lv_color_hex(COL_TXT), true, UI_FONT_SYM);
        lv_display_set_theme(display, theme);
        lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(COL_BG), 0);
        tools_test_battery();
        heading = lv_label_create(lv_layer_top());
        lv_obj_set_style_text_font(heading, UI_FONT_L, 0);
        lv_obj_set_style_text_color(heading, lv_color_hex(COL_TXT), 0);
        lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 46);
        lv_label_set_text(heading, tr_app_name(apps[i]->name));
        lv_obj_t *back = lv_obj_create(lv_layer_top());
        lv_obj_set_size(back, 48, 48); lv_obj_set_style_radius(back, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(back, lv_color_hex(0x16161a), 0);
        lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(back, 1, 0);
        lv_obj_set_style_border_color(back, lv_color_hex(COL_TXT2), 0);
        lv_obj_set_style_border_opa(back, LV_OPA_50, 0);
        lv_obj_set_style_pad_all(back, 0, 0);
        lv_obj_align(back, LV_ALIGN_TOP_MID, -100, 40);
        lv_obj_t *arrow = lv_label_create(back); lv_label_set_text(arrow, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_color(arrow, lv_color_hex(COL_TXT), 0); lv_obj_center(arrow);
        lv_obj_t *page = lv_obj_create(lv_screen_active()); lv_obj_remove_style_all(page);
        lv_obj_set_size(page, 466, 466); ui_obj_set_scrollable(page, false);
        apps[i]->enter(page);
        for (unsigned frame = 0; frame < 60; ++frame) {
            host_time_us += 20000; lv_tick_inc(20);
            if (apps[i]->tick) apps[i]->tick();
            lv_timer_handler();
        }
        capture(argv[1], names[i], display);
        if (apps[i]->exit) apps[i]->exit();
        lv_deinit();
        printf("native %s exported\n", names[i]);
    }
}
