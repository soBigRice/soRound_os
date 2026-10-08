#include "display.h"
#include "board_config.h"
#include "weather_refresh.h"
#include "display_frame_gate.h"

#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_touch_cst9217.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_pm.h"
#include "driver/gpio.h"
#include "freertos/semphr.h"

static const char *TAG = "display";

static esp_lcd_panel_io_handle_t s_io;       // 用于发亮度命令 0x51
static esp_lcd_panel_handle_t    s_panel;    // 用于熄屏/亮屏
static lv_display_t *s_disp;
static SemaphoreHandle_t s_te,s_dma;
static volatile bool s_frame_sending;
static bool s_te_ready,s_te_warned;
static volatile uint32_t s_transfer_count;
static display_frame_gate_t s_frame_gate;
static weather_refresh_t s_refresh;
#if CONFIG_PM_ENABLE
static esp_pm_lock_handle_t s_te_awake;
#endif

static void te_edge(void *arg) {
    (void)arg;BaseType_t wake=pdFALSE;xSemaphoreGiveFromISR(s_te,&wake);if(wake)portYIELD_FROM_ISR();
}
static bool color_done(esp_lcd_panel_io_handle_t io,esp_lcd_panel_io_event_data_t *event,void *arg) {
    (void)io;(void)event;BaseType_t wake=pdFALSE;
    ++s_transfer_count;
    if(s_frame_sending)xSemaphoreGiveFromISR(s_dma,&wake);
    else {
        display_frame_gate_complete(&s_frame_gate);
        lv_display_flush_ready((lv_display_t *)arg);
    }
    return wake==pdTRUE;
}
static void wait_te(void *arg) {
    (void)arg;
    if(!s_te_ready)return;
#if CONFIG_PM_ENABLE
    // GPIO edge ISR cannot wake automatic light sleep. Hold only across this bounded TE wait.
    if(s_te_awake)esp_pm_lock_acquire(s_te_awake);
#endif
    xSemaphoreTake(s_te,0);
    if(xSemaphoreTake(s_te,pdMS_TO_TICKS(25))!=pdTRUE&&!s_te_warned) {
        ESP_LOGW(TAG,"TE timeout; sending the completed weather frame");s_te_warned=true;
    }
#if CONFIG_PM_ENABLE
    if(s_te_awake)esp_pm_lock_release(s_te_awake);
#endif
}
static void send_frame_tile(void *arg,weather_frame_area_t a,uint8_t *pixels) {
    (void)arg;s_frame_sending=true;xSemaphoreTake(s_dma,0);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel,a.x1,a.y1,a.x2+1,a.y2+1,pixels));
    // DMA completion owns the staging-buffer lifetime; never overwrite pixels while it is being read.
    xSemaphoreTake(s_dma,portMAX_DELAY);s_frame_sending=false;
}
static void synchronized_flush(lv_display_t *disp,const lv_area_t *a,uint8_t *pixels) {
    if(weather_refresh_flush(&s_refresh,disp,a,pixels))return;
    // Capture the request when the last tile is submitted, rather than in the
    // ISR. A DMA already in flight before home is revealed cannot satisfy it.
    display_frame_gate_submit(&s_frame_gate,lv_display_flush_is_last(disp));
    // Same asynchronous partial SPI path as esp_lvgl_port 2.8.0 (no rotation or byte swapping on this board).
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel,a->x1,a->y1,a->x2+1,a->y2+1,pixels));
}

void display_weather_mode(bool enabled) {
    if(!s_disp||enabled==(s_refresh.frame.pixels!=NULL))return;
    if(enabled) {
        s_refresh.frame.pixels=heap_caps_calloc(1,WEATHER_FRAME_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!s_refresh.frame.pixels){ESP_LOGW(TAG,"weather frame allocation failed; keeping partial refresh");return;}
        s_refresh.frame.dirty=false;s_te_warned=false;
        if(s_te_ready)gpio_intr_enable(LCD_TE);
        lv_obj_invalidate(lv_screen_active());lv_obj_invalidate(lv_layer_top());
        ESP_LOGI(TAG,"weather frame: %u PSRAM bytes, TE=%s",(unsigned)WEATHER_FRAME_BYTES,s_te_ready?"GPIO13":"unavailable");
    }else {
        if(s_te_ready)gpio_intr_disable(LCD_TE);
        heap_caps_free(s_refresh.frame.pixels);s_refresh.frame=(weather_frame_t){0};
    }
}

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
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

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
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    esp_lcd_panel_invert_color(panel, false);
    esp_lcd_panel_mirror(panel, false, false);
    esp_lcd_panel_disp_on_off(panel, true);
    s_io = io;
    s_panel = panel;
    ESP_LOGI(TAG, "CO5300 panel ready");

    /* 4) lvgl_port:DMA + 内部 RAM 双缓冲流水线(部分刷新)。
       性能三板斧(2026-07 流畅性专项):
       a) LVGL 任务钉核 1 + 优先级 5:WiFi/BT 栈在核 0,渲染不被网络中断打断,帧间抖动小;
       b) 双缓冲 2×40 行(总 RAM 与原单缓冲 80 行相同):渲染下一块与 QSPI DMA 推上一块重叠,
          大面积重绘吞吐 ~×1.5(原单缓冲每块都要等传完才能画下一块);
       c) RGB565_SWAPPED 直渲 + swap_bytes=false:LVGL 9.5 原生按字节序渲染,
          省掉 flush 前对每个像素的 CPU 交换遍历(原来每帧多扫全缓冲一遍)。
          注:LVGL 内部源图(canvas/JPEG 解码 RGB565)由 blend_to_rgb565_swapped 后端转换,无需改动。 */
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 5;
    port_cfg.task_affinity = 1;          // 钉到核 1(协议栈在核 0)
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle    = io,
        .panel_handle = panel,
        .buffer_size  = LCD_H_RES * 40,   // 40 行/块(~36.5KB × 2 = 与原 80 行单缓冲同量级内部 DMA RAM)
        .double_buffer = true,            // ★双缓冲流水线:画 A 推 B 交替,渲染与 DMA 重叠
        .hres         = LCD_H_RES,
        .vres         = LCD_V_RES,
        .monochrome   = false,
        .color_format = LV_COLOR_FORMAT_RGB565_SWAPPED,   // ★直渲面板字节序,免 CPU swap 遍历
        .rotation     = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        .flags = {
            .buff_dma     = true,    // ★缓冲放内部 DMA RAM(DMA 能直接读,这是关键)
            .swap_bytes   = false,   // 已由 RGB565_SWAPPED 直渲取代;颜色若反蓝改回 RGB565+true
            .full_refresh = false,
        },
    };
    // CO5300 不支持 swap_xy(我们本就不需要),但 lvgl_port 初始化会无条件调一次 esp_lcd_panel_swap_xy,
    // 驱动遂刷一条无害的 E "swap_xy is not supported"。旋转之后不再变,故只在这一次 add_disp 期间压掉该 tag。
    esp_log_level_set("co5300_spi", ESP_LOG_NONE);
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    // add_disp unlocks internally. Protect all following LVGL driver/event
    // mutations from its already running render task.
    // The launcher watchdog is not running yet. A stuck first render must
    // fail within a bound so the bootloader can recover a pending OTA.
    ESP_ERROR_CHECK(lvgl_port_lock(5000)?ESP_OK:ESP_ERR_TIMEOUT);
    assert(disp);s_disp=disp;s_dma=xSemaphoreCreateBinary();s_te=xSemaphoreCreateBinary();assert(s_dma&&s_te);
    esp_lcd_panel_io_callbacks_t callbacks={.on_color_trans_done=color_done};
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io,&callbacks,disp));
    s_refresh.wait_te=wait_te;s_refresh.send=send_frame_tile;
    lv_display_set_flush_cb(disp,synchronized_flush);
    gpio_config_t te={.pin_bit_mask=1ULL<<LCD_TE,.mode=GPIO_MODE_INPUT,.intr_type=GPIO_INTR_POSEDGE};
    esp_err_t isr=gpio_install_isr_service(0);
    s_te_ready=(isr==ESP_OK||isr==ESP_ERR_INVALID_STATE)&&gpio_config(&te)==ESP_OK&&gpio_isr_handler_add(LCD_TE,te_edge,NULL)==ESP_OK;
    if(!s_te_ready)ESP_LOGW(TAG,"TE setup unavailable; complete-frame refresh remains enabled for weather");
    else gpio_intr_disable(LCD_TE);
#if CONFIG_PM_ENABLE
    if(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP,0,"weather_te",&s_te_awake)!=ESP_OK)
        ESP_LOGW(TAG,"TE light-sleep guard unavailable");
#endif
    esp_log_level_set("co5300_spi", ESP_LOG_INFO);   // 恢复,后续真有错误照常打印
    lv_display_add_event_cb(disp, rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    lvgl_port_unlock();
    return disp;
}

void display_set_brightness(uint8_t level) {
    if (!s_io) return;
    // CO5300 走 QSPI:命令必须按 QSPI 编码(写命令 opcode 0x02<<24 | cmd<<8),
    // 直接发裸 0x51 面板收不到 —— 这就是之前亮度/锁屏变暗不生效的根因。
    esp_lcd_panel_io_tx_param(s_io, (0x02 << 24) | (0x51 << 8), (uint8_t[]){ level }, 1);
}

void display_sleep(bool sleep) {
    if (s_panel) esp_lcd_panel_disp_on_off(s_panel, !sleep);
}

uint32_t display_transfer_count(void){return s_transfer_count;}
uint32_t display_request_frame(void) {
    uint32_t request=display_frame_gate_request(&s_frame_gate);
    lv_obj_invalidate(lv_screen_active());lv_obj_invalidate(lv_layer_top());
    return request;
}
bool display_frame_completed(uint32_t request){return display_frame_gate_done(&s_frame_gate,request);}
bool touch_init(i2c_master_bus_handle_t i2c_bus, lv_display_t *disp) {
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
    if(!lvgl_port_add_touch(&touch_cfg)){ESP_LOGE(TAG,"Touch input allocation failed");return false;}
    ESP_LOGI(TAG, "CST9217 touch ready");
    return true;
}
