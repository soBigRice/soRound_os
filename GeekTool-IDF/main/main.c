/*
 * GeekTool-IDF —— M2:启动器 + App 框架(LVGL 9)
 * 显示底层(display.c)沿用 M1 已验证配置(CO5300 QSPI 80MHz + 单缓冲内部 DMA)。
 */
#include <stdlib.h>
#include <time.h>
#include "nvs_flash.h"
#include "driver/i2c_master.h"
#include "esp_lvgl_port.h"
#include "esp_pm.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"

#include "board_config.h"
#include "display.h"
#include "app.h"
#include "settings.h"
#include "rtc.h"
#include "audio_bus.h"
#include "img_store.h"
#include "imu.h"
#include "identity_ui.h"
#include "startup.h"
#include "startup_network.h"
#include "watchface.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static const char *TAG = "main";
enum { STARTUP_UI_LOCK_MS = 5000 };

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
    // sw 包含 OTA 和渲染看门狗调用 esp_restart；USB 调试复位要单独记录。
    // 结合OTA/渲染看门狗阶段日志区分原因；仅凭sw不能判断是哪条路径触发。
    static const char *const RRS[] = { "unknown","poweron","ext","sw","panic","int_wdt","task_wdt","wdt","deepsleep","brownout","sdio","usb","jtag","efuse","power_glitch","cpu_lockup" };
    esp_reset_reason_t rr = esp_reset_reason();
    ESP_LOGW(TAG, "last reset: %s", (unsigned)rr < sizeof(RRS) / sizeof(RRS[0]) ? RRS[rr] : "unknown");

    ESP_ERROR_CHECK(nvs_flash_init());
    setenv("TZ", "CST-8", 1); tzset();           // 中国时区,供表盘 localtime 用

    // 省电:动态调频(空闲 240→80MHz)+ 自动 light sleep(需 sdkconfig 开 TICKLESS_IDLE)。
    // 各外设驱动(SPI 推屏/I2C/I2S/WiFi/BT)在忙时自动持 pm 锁,不会睡在事务中间;
    // USB 插着时不睡(USJ_NO_AUTO_LS_ON_CONNECTION),调试不掉线。
    // ★若出现触摸丢事件/屏幕异常,先把 light_sleep_enable 改回 false 定位。
    esp_pm_config_t pm = { .max_freq_mhz = 240, .min_freq_mhz = 80, .light_sleep_enable = true };
    esp_pm_configure(&pm);

    audio_bus_init();
    s_i2c_bus = init_i2c();
    imu_init();                     // 锁外预热传感器;稳定期由读取端检查,不阻塞 UI
    rtc_begin();
    rtc_sync_to_system();            // RTC → 系统时间(断电/无网也走时;之后 SNTP 会再校准并写回)
    lv_display_t *disp = display_init();
    settings_init();                 // 读 NVS + 应用亮度(需 display 已 init)
    i18n_init();                     // 语言表 + CJK fallback 字体(需在建主题/launcher 前)
    bool touch_ready=touch_init(s_i2c_bus,disp);

    lv_obj_t *boot=NULL;bool launcher_ready=false;
    ESP_LOGI(TAG,"startup stage=UI begin touch=%d",touch_ready);
    uint32_t first_heartbeat=0,first_transfer=0;int64_t cover_started_us=0;
    // Before launcher_start there is no render watchdog. An infinite mutex
    // wait here would prevent both the self-check and pending-OTA rollback.
    bool ui_locked=lvgl_port_lock(STARTUP_UI_LOCK_MS);
    if (ui_locked) {
        // 全局 Nothing 单色暗色主题(红强调);默认字体用带符号的 montserrat,
        // 让键盘/按钮等默认控件也统一风格(各 app 的正文再单独覆盖成点阵字)
        lv_theme_t *th = lv_theme_default_init(disp, lv_color_hex(COL_RED),
                                               lv_color_hex(COL_TXT), true, UI_FONT_SYM);
        lv_display_set_theme(disp, th);
        // Build the cover before interactive pages; allocation failure cannot
        // expose a partially initialized home. The pending OTA then rolls back.
        boot=identity_boot_create(lv_layer_top());
        if(boot) {
            cover_started_us=esp_timer_get_time();
            identity_boot_message(boot,settings_lang()?"系统检查中":"System check...",false);
            launcher_ready=launcher_start();
            lv_obj_move_foreground(boot);
            first_heartbeat=launcher_heartbeat();first_transfer=display_transfer_count();
        }
        lvgl_port_unlock();
    }
    ESP_ERROR_CHECK(ui_locked?(boot?ESP_OK:ESP_ERR_NO_MEM):ESP_ERR_TIMEOUT);
    ESP_LOGI(TAG,"startup stage=UI created launcher=%d",launcher_ready);

    img_store_face_image();          // 锁外后台预热图片,首次切表盘不阻塞 UI
    wifi_service_start();            // 开机自动起 WiFi + 重连记住的 AP(不碰 LVGL,放锁外)

    startup_result_t check=startup_selftest(touch_ready,launcher_ready,first_heartbeat,first_transfer,cover_started_us);
    ESP_LOGI(TAG,"startup core=%s err=%s",startup_problem_name(check.problem),esp_err_to_name(check.error));
    if(check.problem==STARTUP_OK) {
        startup_network_result_t network=startup_network_check(settings_beta()!=0);
        ESP_LOGI(TAG,"startup network=%d err=%s",network.state,esp_err_to_name(network.error));
        if(network.state!=STARTUP_NET_READY) {
            ESP_LOGW(TAG,"Startup network unavailable; offline use allowed");
            if(lvgl_port_lock(STARTUP_UI_LOCK_MS)) {
                const char *message=network.state==STARTUP_NET_OFFLINE?
                    (settings_lang()?"网络未就绪\n可离线使用":"Network not ready\nOffline available"):
                    (settings_lang()?"联网检查失败\n可离线使用":"Network check failed\nOffline available");
                identity_boot_message(boot,message,false);lvgl_port_unlock();
            }
            vTaskDelay(pdMS_TO_TICKS(1200));
        }
        uint32_t request=0;
        if(lvgl_port_lock(STARTUP_UI_LOCK_MS)) {
            watchface_select(settings_face());
            identity_boot_reveal(boot);
            first_heartbeat=launcher_heartbeat();request=display_request_frame();
            lvgl_port_unlock();
        }
        check=startup_wait_home(first_heartbeat,request);
        ESP_LOGI(TAG,"startup home frame=%u completed=%d heartbeat=%u",
            (unsigned)request,display_frame_completed(request),(unsigned)launcher_heartbeat());
    }
    ESP_LOGI(TAG,"startup check=%s err=%s",startup_problem_name(check.problem),esp_err_to_name(check.error));
    if(check.problem!=STARTUP_OK && lvgl_port_lock(STARTUP_UI_LOCK_MS)) {
        char message[96];snprintf(message,sizeof message,"%s\n%s",settings_lang()?"启动自检失败":"Startup check failed",startup_problem_name(check.problem));
        identity_boot_message(boot,message,true);lvgl_port_unlock();
    }
    startup_ota_result_t ota=startup_apply_ota_result(check);
    ESP_LOGI(TAG,"startup OTA action=%d err=%s",ota.action,esp_err_to_name(ota.error));
    if(check.problem!=STARTUP_OK || ota.error!=ESP_OK) {
        if(ota.error!=ESP_OK && lvgl_port_lock(STARTUP_UI_LOCK_MS)) {
            const char *code=ota.action==STARTUP_OTA_CONFIRM_ERROR?"OTA CONFIRM":
                ota.action==STARTUP_OTA_ROLLBACK_ERROR?"ROLLBACK":"OTA STATE";
            char message[96];snprintf(message,sizeof message,"%s\n%s",settings_lang()?"启动自检失败":"Startup check failed",code);
            identity_boot_message(boot,message,true);lvgl_port_unlock();
        }
        ESP_LOGE(TAG,"Startup stopped; home remains covered. No unconditional reboot loop.");
        return;
    }
    if(!lvgl_port_lock(STARTUP_UI_LOCK_MS)) {
        ESP_LOGE(TAG,"Startup cover release timed out");return;
    }
    identity_boot_release(boot);lvgl_port_unlock();

    ESP_LOGI(TAG, "GeekTool M2a up — 左右滑/箭头切换,点图标进入,app 内右滑/‹ 返回");
}
