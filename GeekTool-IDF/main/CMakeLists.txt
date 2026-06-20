idf_component_register(
    SRCS "main.c" "display.c" "launcher.c" "power.c" "watchface.c" "lock.c"
         "ui_list.c" "app_sys.c" "app_i2c.c" "app_wifi.c" "app_ota.c"
    INCLUDE_DIRS "."
    REQUIRES esp_lcd esp_driver_spi esp_driver_i2c esp_driver_gpio esp_lvgl_port nvs_flash
             esp_timer esp_psram spi_flash esp_hw_support esp_system esp_common
             esp_wifi esp_netif esp_event lwip
             esp_https_ota app_update esp_http_client esp-tls mbedtls
)
