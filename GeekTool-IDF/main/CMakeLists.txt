idf_component_register(
    SRCS "main.c" "display.c" "launcher.c" "power.c" "watchface.c" "lock.c" "glyph.c"
         "ui_list.c" "app_sys.c" "app_i2c.c" "app_wifi.c" "app_ota.c" "app_weather.c"
    INCLUDE_DIRS "."
    REQUIRES esp_lcd esp_driver_spi esp_driver_i2c esp_driver_gpio esp_lvgl_port nvs_flash
             esp_timer esp_psram spi_flash esp_hw_support esp_system esp_common
             esp_wifi esp_netif esp_event lwip
             esp_https_ota app_update esp_http_client esp-tls mbedtls
)

# GCC 的 -Wformat-truncation 对 snprintf 的小缓冲过度保守(把 %d 当成最长 11 字符),
# 我们的温度/时间/地址数值都很小,属误报。对本组件关掉这个告警,免得 watchface/app_sys 也被卡。
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format-truncation)
