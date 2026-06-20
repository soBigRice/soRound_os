// AXP2101 电源读取。只读不写,不碰充电参数配置(那是另一档事)。
#include "power.h"
#include "board_config.h"     // 带入 driver/i2c_master.h + board_i2c_bus()
#include "esp_log.h"

#define AXP2101_ADDR  0x34

static i2c_master_dev_handle_t s_dev;

static bool rd_reg(uint8_t reg, uint8_t *val) {
    return s_dev && i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 50) == ESP_OK;
}

static bool wr_reg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg, val };
    return s_dev && i2c_master_transmit(s_dev, buf, 2, 50) == ESP_OK;
}

void power_init(void) {
    if (s_dev) return;
    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = AXP2101_ADDR,
        .scl_speed_hz    = 400000,
    };
    if (i2c_master_bus_add_device(board_i2c_bus(), &dc, &s_dev) != ESP_OK) {
        s_dev = NULL;
        ESP_LOGW("power", "AXP2101 add device failed");
    }
}

void power_off(void) {
    uint8_t v;
    if (rd_reg(0x10, &v)) wr_reg(0x10, v | 0x01);   // 置 SOFT_OFF 位
}

// PWRON 键 IRQ:短按=INTSTS2(0x49)bit3,长按=bit2;写 1 清。使能在 INTEN2(0x41)。
void power_key_init(void) {
    wr_reg(0x48, 0xFF); wr_reg(0x49, 0xFF); wr_reg(0x4A, 0xFF);   // 清所有 IRQ
    uint8_t en;
    if (rd_reg(0x41, &en)) wr_reg(0x41, en | 0x0C);              // 使能短按(bit3)+长按(bit2)
}

int power_key_event(void) {
    uint8_t s;
    if (!rd_reg(0x49, &s)) return 0;
    if (s & 0x08) { wr_reg(0x49, 0x08); return 1; }   // 短按
    if (s & 0x04) { wr_reg(0x49, 0x04); return 2; }   // 长按
    return 0;
}

bool power_read(int *soc, pwr_state_t *st) {
    uint8_t s2, lvl;
    if (!rd_reg(0x01, &s2) || !rd_reg(0xA4, &lvl)) return false;

    int  dir  = (s2 & 0x60) >> 5;       // 0x01[6:5]:1=充电,2=放电
    bool done = (s2 & 0x07) == 0x04;    // 0x01[2:0]==100:充电完成

    if (soc) *soc = lvl;                // 0xA4:电量百分比 0-100
    if (st) {
        if (done)          *st = PWR_FULL;
        else if (dir == 1) *st = PWR_CHARGING;
        else if (dir == 2) *st = PWR_DISCHARGING;
        else               *st = PWR_UNKNOWN;
    }
    return true;
}
