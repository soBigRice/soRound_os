#include "display.h"
#include "board_config.h"

#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_touch_cst9217.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"

static const char *TAG = "display";

static esp_lcd_panel_io_handle_t s_io;       // 用于发亮度命令 0x51
static esp_lcd_panel_handle_t    s_panel;    // 用于熄屏/亮屏

/* CO5300 厂商初始化序列(QSPI 模式)—— 来自小智官方对本板的验证 */
static const co5300_lcd_init_cmd_t vendor_specific_init[] = {
    {0xFE, (uint8_t[]){0x20}, 1, 0},
    {0x19, (uint8_t[]){0x10}, 1, 0},
    {0x1C, (uint8_t[]){0xA0}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},   // 16 bit/px
    {0x35, (uint8_t[]){0x00}, 1, 0},   // TE on
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},   // 亮度最大
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x06, 0x01, 0xD7}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 600},
    {0x11, NULL, 0, 600},              // sleep out
    {0x29, NULL, 0, 0},                // display on
};

/* CO5300 要求刷新窗口偶数对齐:x1/y1 向下取偶,x2/y2 向上取奇 */
static void rounder_cb(lv_event_t *e) {
    lv_area_t *a = (lv_area_t *)lv_event_get_param(e);
    a->x1 = (a->x1 >> 1) << 1;
    a->y1 = (a->y1 >> 1) << 1;
    a->x2 = ((a->x2 >> 1) << 1) + 1;
    a->y2 = ((a->y2 >> 1) << 1) + 1;
}

lv_display_t *display_init(void) {
    /* 1) QSPI 总线 */
    spi_bus_config_t buscfg = {
        .sclk_io_num  = LCD_QSPI_PCLK,
        .data0_io_num = LCD_QSPI_D0,
        .data1_io_num = LCD_QSPI_D1,
        .data2_io_num = LCD_QSPI_D2,
        .data3_io_num = LCD_QSPI_D3,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
        .flags = SPICOMMON_BUSFLAG_QUAD,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));  // 开 DMA(无撕裂的关键)

    /* 2) 面板 IO */
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = CO5300_PANEL_IO_QSPI_CONFIG(LCD_QSPI_CS, NULL, NULL);
    io_cfg.pclk_hz = 80 * 1000 * 1000;   // ★QSPI 拉到 80MHz,推屏吞吐翻倍(若出现花屏/雪花,降到 60 或 40MHz)
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_cfg, &io));

    /* 3) CO5300 面板 */
    const co5300_vendor_config_t vendor_cfg = {
        .init_cmds      = vendor_specific_init,
        .init_cmds_size = sizeof(vendor_specific_init) / sizeof(co5300_lcd_init_cmd_t),
        .flags = { .use_qspi_interface = 1 },
    };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BITS_PER_PIXEL,
        .vendor_config  = (void *)&vendor_cfg,
    };
    esp_lcd_panel_handle_t panel = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_co5300(io, &panel_cfg, &panel));
    esp_lcd_panel_set_gap(panel, LCD_GAP_X, 0);
    esp_lcd_panel_reset(panel);
    esp_lcd_panel_init(panel);
    esp_lcd_panel_invert_color(panel, false);
    esp_lcd_panel_mirror(panel, false, false);
    esp_lcd_panel_disp_on_off(panel, true);
    s_io = io;
    s_panel = panel;
    ESP_LOGI(TAG, "CO5300 panel ready");

    /* 4) lvgl_port:DMA + 内部 RAM 双缓冲(部分刷新)。先确认 DMA 能跑顺;
       之后追求"零撕裂"再上全屏 PSRAM 缓冲(见 PORTING_NOTES)。 */
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle    = io,
        .panel_handle = panel,
        .buffer_size  = LCD_H_RES * 160,  // 单缓冲放大到 160 行
        .double_buffer = false,           // ★单缓冲:消除双缓冲交替造成的闪烁(小智 SPI 屏也用单缓冲)
        .hres         = LCD_H_RES,
        .vres         = LCD_V_RES,
        .monochrome   = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation     = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        .flags = {
            .buff_dma     = true,    // ★缓冲放内部 DMA RAM(DMA 能直接读,这是关键)
            .swap_bytes   = true,    // 若颜色偏蓝/反色,改 false
            .full_refresh = false,
        },
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    lv_display_add_event_cb(disp, rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    return disp;
}

void display_set_brightness(uint8_t level) {
    if (s_io) esp_lcd_panel_io_tx_param(s_io, 0x51, (uint8_t[]){ level }, 1);
}

void display_sleep(bool sleep) {
    if (s_panel) esp_lcd_panel_disp_on_off(s_panel, !sleep);
}

void touch_init(i2c_master_bus_handle_t i2c_bus, lv_display_t *disp) {
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
    tp_io_cfg.scl_speed_hz = 400 * 1000;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_cfg, &tp_io));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_H_RES - 1,
        .y_max = LCD_V_RES - 1,
        .rst_gpio_num = TOUCH_RST,
        .int_gpio_num = TOUCH_INT,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = 0, .mirror_x = 1, .mirror_y = 1 },
    };
    esp_lcd_touch_handle_t tp = NULL;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst9217(tp_io, &tp_cfg, &tp));

    lvgl_port_touch_cfg_t touch_cfg = { .disp = disp, .handle = tp };
    lvgl_port_add_touch(&touch_cfg);
    ESP_LOGI(TAG, "CST9217 touch ready");
}
