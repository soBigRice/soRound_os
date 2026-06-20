/*
 * GeekTool-IDF —— M2:启动器 + App 框架(LVGL 9)
 * 显示底层(display.c)沿用 M1 已验证配置(CO5300 QSPI 80MHz + 单缓冲内部 DMA)。
 */
#include <stdlib.h>
#include <time.h>
#include "nvs_flash.h"
#include "driver/i2c_master.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"

#include "board_config.h"
#include "display.h"
#include "app.h"

static const char *TAG = "main";

static i2c_master_bus_handle_t s_i2c_bus;   // 共享给各 app(I2C 扫描器)
i2c_master_bus_handle_t board_i2c_bus(void) { return s_i2c_bus; }

static i2c_master_bus_handle_t init_i2c(void) {
    i2c_master_bus_config_t cfg = {
        .i2c_port   = I2C_NUM_0,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus = NULL;
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &bus));
    return bus;
}

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    setenv("TZ", "CST-8", 1); tzset();           // 中国时区,供表盘 localtime 用

    s_i2c_bus = init_i2c();
    lv_display_t *disp = display_init();
    touch_init(s_i2c_bus, disp);

    if (lvgl_port_lock(0)) {
        // 全局 Nothing 单色暗色主题(红强调);默认字体用带符号的 montserrat,
        // 让键盘/按钮等默认控件也统一风格(各 app 的正文再单独覆盖成点阵字)
        lv_theme_t *th = lv_theme_default_init(disp, lv_color_hex(COL_RED),
                                               lv_color_hex(COL_TXT), true, UI_FONT_SYM);
        lv_display_set_theme(disp, th);
        launcher_start();
        lvgl_port_unlock();
    }
    ESP_LOGI(TAG, "GeekTool M2a up — 左右滑/箭头切换,点图标进入,app 内右滑/‹ 返回");
}
