// AXP2101 电源:读电量/状态 + 充电策略(配置充电参数 + 按 die 温度自适应限流:凉快、温中、烫慢)。
#include "power.h"
#include "board_config.h"     // 带入 driver/i2c_master.h + board_i2c_bus()
#include "esp_log.h"

#define AXP2101_ADDR  0x34

// 充电电流档位(写 REG 0x62 bits[4:0];≤200mA 是 25mA 步进、之后 100mA 步进):0x08=200 0x09=300 0x0A=400mA
#define CHG_FAST  0x0A      // die 凉:400mA(比原 xiaozhi 验证的 200mA 快一倍)
#define CHG_MID   0x09      // die 温:300mA
#define CHG_SLOW  0x08      // die 烫:200mA(回到本板验证过的保守值,绝不更高)
#define T_WARM    48.0f     // die ≥48°C → 降到 MID
#define T_HOT     56.0f     // die ≥56°C → 降到 SLOW
#define T_HYST    4.0f      // 回升迟滞,防档位来回抖

static i2c_master_dev_handle_t s_dev;
static int     s_tier;            // 当前温度档:0=FAST 1=MID 2=SLOW
static uint8_t s_chg_code = 0;    // 已写入的 0x62 电流档(变了才写,省 I2C)

static bool rd_reg(uint8_t reg, uint8_t *val) {
    return s_dev && i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 50) == ESP_OK;
}

static bool wr_reg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg, val };
    return s_dev && i2c_master_transmit(s_dev, buf, 2, 50) == ESP_OK;
}

// 读 AXP2101 内部 die 温度(14-bit:0x3C 高6 / 0x3D 低8;T = 22 + (7274-raw)/20,取自 XPowersLib)
static bool read_die_temp(float *t) {
    uint8_t h, l;
    if (!rd_reg(0x3C, &h) || !rd_reg(0x3D, &l)) return false;
    uint16_t raw = ((uint16_t)(h & 0x3F) << 8) | l;
    if (raw > 16383) return false;
    *t = 22.0f + (7274.0f - (float)raw) / 20.0f;
    return true;
}

// 充电参数:CV 4.2V / 预充 50mA / 终止 25mA;使能 die 温度 ADC。
// 之前沿用 xiaozhi 示例的 4.1V 保守档,电量计常停在约 95%;普通 4.2V 锂电改 4.2V 才更接近 100%。
// 输入限流【保持芯片默认】(本板上电默认值已验证可用):之前抬到 900mA,插弱源时音频上电瞬间易过流拉崩 3.3V,
// 故撤回。充电电流仍设 400mA,实际由默认输入档 + VinDPM 自然封顶,比原来的慢速默认仍快不少,且不会过流。
static void charge_config(void) {
    wr_reg(0x64, 0x03);                                        // CV 充电目标电压 4.2V(AXP2101:0x02=4.1V,0x03=4.2V)
    wr_reg(0x61, 0x02);                                        // 预充电流 50mA
    wr_reg(0x63, 0x01);                                        // 终止电流 25mA
    uint8_t v;
    if (rd_reg(0x30, &v)) wr_reg(0x30, v | (1 << 4));          // 使能内部温度 ADC(读 die 温度)
    if (rd_reg(0x62, &v)) wr_reg(0x62, (v & 0xE0) | CHG_FAST); // 初始充电电流 = 快档(由默认输入限流封顶)
    s_tier = 0; s_chg_code = CHG_FAST;
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
        return;
    }
    charge_config();                                          // 配充电参数 + 使能 die 温度 ADC
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

// 音频 codec(ES7210/ES8311)由 AXP2101 ALDO1 供电。只开 ALDO1、不动其它轨(别影响已工作的显示)。
void power_audio_on(void) {
    wr_reg(0x92, (3300 - 500) / 100);                 // ALDO1 = 3.3V
    uint8_t v;
    if (rd_reg(0x90, &v)) wr_reg(0x90, v | 0x01);     // 使能 ALDO1(bit0),保留其它 LDO 状态
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

// 充电策略:按 die 温度自适应调充电电流(凉=快 / 温=中 / 烫=慢,带 4°C 迟滞)。
// 由周期定时器调(launcher 的 2s 电量定时器),die 温度变化慢,2s 足够。
void power_charge_govern(void) {
    if (!s_dev) return;
    float t;
    if (!read_die_temp(&t)) return;
    if (s_tier < 2 && t >= (s_tier == 0 ? T_WARM : T_HOT)) s_tier++;                  // 变烫 → 降档
    else if (s_tier > 0 && t <= (s_tier == 1 ? T_WARM - T_HYST : T_HOT - T_HYST)) s_tier--;  // 凉回来 → 升档
    uint8_t code = (s_tier == 0) ? CHG_FAST : (s_tier == 1) ? CHG_MID : CHG_SLOW;
    if (code != s_chg_code) {
        uint8_t v;
        if (rd_reg(0x62, &v) && wr_reg(0x62, (v & 0xE0) | code)) {
            s_chg_code = code;
            ESP_LOGI("power", "die %.0fC -> charge %dmA", (double)t,
                     code == CHG_FAST ? 400 : code == CHG_MID ? 300 : 200);
        }
    }
}
