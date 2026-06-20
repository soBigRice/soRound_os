// I2C 扫描器 —— 扫出板载芯片地址,带已知芯片名
#include "app.h"
#include "ui_list.h"
#include <Wire.h>

static const char *i2c_name(uint8_t a) {
  switch (a) {
    case 0x18: return "ES8311 codec";
    case 0x34: return "AXP2101 PMU";
    case 0x40: return "ES7210 ADC";
    case 0x51: return "PCF85063 RTC";
    case 0x5a: return "CST9217 touch";
    case 0x6a:
    case 0x6b: return "QMI8658 IMU";
    default:   return "";
  }
}

static void i2c_enter(lv_obj_t *parent) {
  lv_obj_t *list = ui_list_create(parent);

  int found = 0;
  for (uint8_t a = 0x08; a < 0x78; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      char addr[8];
      snprintf(addr, sizeof(addr), "0x%02X", a);
      const char *nm = i2c_name(a);
      ui_list_row(list, addr, nm[0] ? nm : "?", nm[0] ? COL_OK : COL_TXT2);
      found++;
    }
  }
  if (found == 0) ui_list_row(list, "No I2C device", NULL, 0);

  ui_list_relayout(list);
  Serial.printf("[I2C] found %d device(s)\n", found);
}

const app_t app_i2c = { "I2C", COL_I2C, i2c_enter, NULL, NULL };
