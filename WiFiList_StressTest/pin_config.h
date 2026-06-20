#pragma once
// ESP32-S3-Touch-AMOLED-1.75C 引脚定义(取自微雪官方 Mylibrary/pin_config.h)

#define XPOWERS_CHIP_AXP2101

// 显示(QSPI / CO5300)
#define LCD_SDIO0 4
#define LCD_SDIO1 5
#define LCD_SDIO2 6
#define LCD_SDIO3 7
#define LCD_SCLK  38
#define LCD_RESET 2
#define LCD_CS    12
#define LCD_WIDTH  466
#define LCD_HEIGHT 466

// 触摸 / I2C(CST9217,与 IMU、AXP2101 共用 I2C)
#define IIC_SDA 15
#define IIC_SCL 14
#define TP_INT  11
#define TP_RST  2     // 与 LCD_RESET 共用 GPIO2

// 音频(I2S)
#define PIN_ES7210_BCLK 9
#define PIN_ES7210_LRCK 45
#define PIN_ES7210_DIN  10
#define PIN_ES7210_MCLK 16
#define PIN_ES8311_DOUT 8
#define PA              46
