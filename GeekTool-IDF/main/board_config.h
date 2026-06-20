#pragma once
// GeekTool-IDF —— ESP32-S3-Touch-AMOLED-1.75C 板级引脚
// 取自小智官方板级配置(1.75C 分支)。注意:与 Arduino 版的复位脚不同!
#include "driver/i2c_master.h"

// ---- 显示 QSPI(CO5300) ----
#define LCD_QSPI_CS    12
#define LCD_QSPI_PCLK  38
#define LCD_QSPI_D0    4
#define LCD_QSPI_D1    5
#define LCD_QSPI_D2    6
#define LCD_QSPI_D3    7
#define LCD_RST        1            // ← 1.75C = GPIO1(非 C 是 39)
#define LCD_H_RES      466
#define LCD_V_RES      466
#define LCD_GAP_X      0x06         // 列偏移(对应 set_gap(0x06,0))
#define LCD_BITS_PER_PIXEL 16

// ---- 触摸(CST9217,I2C) ----
#define TOUCH_RST      2
#define TOUCH_INT      11

// ---- I2C(触摸 / TCA9554 / 音频 codec 共用) ----
#define I2C_SDA        15
#define I2C_SCL        14
#define TCA9554_ADDR   ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000   // 0x20

// ---- 音频 I2S(1.75C) ----
#define AUDIO_MCLK     16
#define AUDIO_WS       45
#define AUDIO_BCLK     9
#define AUDIO_DIN      10          // 麦克风(ES7210)
#define AUDIO_DOUT     8           // 扬声器(ES8311)
#define AUDIO_PA       46         // 功放使能

// ---- 按键 ----
#define BOOT_BUTTON    0

// ---- 板级服务 ----
// 共享 I2C 总线句柄(在 main.c 创建);供 I2C 扫描器等 app 使用
i2c_master_bus_handle_t board_i2c_bus(void);
