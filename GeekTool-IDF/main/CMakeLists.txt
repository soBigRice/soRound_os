idf_component_register(
    SRCS "main.c" "display.c" "launcher.c" "power.c" "watchface.c" "lock.c" "glyph.c" "bootkey.c"
         "quickpanel.c"
         "ui_list.c" "app_sys.c" "app_i2c.c" "app_wifi.c" "app_weather.c"
         "settings.c" "app_settings.c" "app_ota.c" "audio_mic.c" "app_audio.c" "app_calendar.c"
         "img_store.c" "app_countdown.c" "audio_out.c"
         "rtc.c" "imu.c" "app_level.c" "app_maze.c" "app_stopwatch.c" "app_fluid.c" "app_dice.c"
         "ble_twin.c" "app_twin.c"
    INCLUDE_DIRS "."
    REQUIRES esp_lcd esp_driver_spi esp_driver_i2c esp_driver_gpio esp_lvgl_port nvs_flash
             esp_timer esp_psram spi_flash esp_hw_support esp_system esp_common
             esp_wifi esp_netif esp_event lwip
             bt
             esp_http_client esp-tls mbedtls esp_https_ota app_update
             esp_driver_i2s esp_codec_dev
             fatfs esp_jpeg
)

# 图片表盘:把 images/ 目录打包成 storage 分区的只读 FAT 镜像,随 idf.py flash 一起烧入。
# 用户把 466x466 基线 bg.jpg 放进 ../images/ 后重新 flash 即可换图。
fatfs_create_rawflash_image(storage ../images FLASH_IN_PROJECT)

# GCC 的 -Wformat-truncation 对 snprintf 的小缓冲过度保守(把 %d 当成最长 11 字符),
# 我们的温度/时间/地址数值都很小,属误报。对本组件关掉这个告警,免得 watchface/app_sys 也被卡。
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format-truncation)
