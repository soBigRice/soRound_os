// PCF85063 RTC —— 见 rtc.h。时间寄存器从 0x04 开始(秒/分/时/日/周/月/年,BCD)。
// 秒寄存器 bit7 = OS(振荡器停过 → 时间无效)。本机时区 CST-8 在 main 里设,RTC 存本地时间。
#include "rtc.h"
#include "board_config.h"     // 带入 i2c_master.h + board_i2c_bus()
#include "esp_log.h"
#include <time.h>
#include <sys/time.h>
#include <string.h>

#define PCF_ADDR 0x51
static const char *TAG = "rtc";
static i2c_master_dev_handle_t s_dev;

static uint8_t b2d(uint8_t b) { return (uint8_t)((b >> 4) * 10 + (b & 0x0f)); }
static uint8_t d2b(uint8_t d) { return (uint8_t)(((d / 10) << 4) | (d % 10)); }

static bool rd(uint8_t reg, uint8_t *buf, size_t n) {
    return s_dev && i2c_master_transmit_receive(s_dev, &reg, 1, buf, n, 100) == ESP_OK;
}
static bool wr(uint8_t reg, const uint8_t *data, size_t n) {
    uint8_t tmp[12];
    if (n + 1 > sizeof tmp) return false;
    tmp[0] = reg;
    memcpy(tmp + 1, data, n);
    return s_dev && i2c_master_transmit(s_dev, tmp, n + 1, 100) == ESP_OK;
}

void rtc_begin(void) {
    if (s_dev) return;
    i2c_device_config_t dc = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = PCF_ADDR, .scl_speed_hz = 400000 };
    if (i2c_master_bus_add_device(board_i2c_bus(), &dc, &s_dev) != ESP_OK) {
        s_dev = NULL;
        ESP_LOGW(TAG, "PCF85063 add device failed");
    }
}

bool rtc_sync_to_system(void) {
    if (!s_dev) return false;
    uint8_t b[7];
    if (!rd(0x04, b, 7)) return false;
    if (b[0] & 0x80) { ESP_LOGW(TAG, "OS set — time invalid"); return false; }   // 掉电过,没有有效时间
    struct tm tm = { 0 };
    tm.tm_sec  = b2d(b[0] & 0x7f);
    tm.tm_min  = b2d(b[1] & 0x7f);
    tm.tm_hour = b2d(b[2] & 0x3f);
    tm.tm_mday = b2d(b[3] & 0x3f);
    tm.tm_wday = b[4] & 0x07;
    tm.tm_mon  = b2d(b[5] & 0x1f) - 1;
    tm.tm_year = b2d(b[6]) + 100;            // 2000+yy → 自 1900 的年数
    if (tm.tm_year < 124) return false;       // < 2024 视为没设过
    time_t t = mktime(&tm);                    // 按 TZ(CST-8)把本地时间转 epoch
    struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    ESP_LOGI(TAG, "restored %04d-%02d-%02d %02d:%02d:%02d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return true;
}

void rtc_save_from_system(void) {
    if (!s_dev) return;
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    if (tm.tm_year < 124) return;             // 系统时间还没校准,别把垃圾写进 RTC
    uint8_t b[7];
    b[0] = d2b(tm.tm_sec) & 0x7f;             // bit7=0 → 清 OS,标记有效
    b[1] = d2b(tm.tm_min);
    b[2] = d2b(tm.tm_hour);
    b[3] = d2b(tm.tm_mday);
    b[4] = (uint8_t)tm.tm_wday;
    b[5] = d2b(tm.tm_mon + 1);
    b[6] = d2b(tm.tm_year - 100);
    uint8_t ctrl1 = 0x00;
    wr(0x00, &ctrl1, 1);                       // Control_1:运行、24 小时制
    if (wr(0x04, b, 7)) ESP_LOGI(TAG, "saved to RTC");
}
