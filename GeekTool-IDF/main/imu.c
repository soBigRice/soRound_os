// QMI8658 加速度计 —— 见 imu.h。寄存器:WHO_AM_I(0x00)=0x05;CTRL1(0x02)/CTRL2(0x03)/CTRL7(0x08);
// 加速度输出 0x35..0x3A(Ax_L..Az_H,小端 16bit)。±2g → 16384 LSB/g。
#include "imu.h"
#include "board_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "imu";
static i2c_master_dev_handle_t s_dev;
static bool s_ok;
static int64_t s_last_try_us;

#define IMU_RETRY_US (5LL * 1000 * 1000)

// 屏幕平面映射(屏幕:右=+x,下=+y)。实测本板:Z 垂直屏幕;右边压低→ay↑、下边压低→ax↑。
// 所以屏幕两轴是【交换】的:右=芯片+Y,下=芯片+X(符号都为正)。平放时两者≈0=中立。
#define MAP_TX(ax, ay, az)  (ay)     // 屏幕右(+x)= 芯片 +Y
#define MAP_TY(ax, ay, az)  (ax)     // 屏幕下(+y)= 芯片 +X

static bool rd(uint8_t reg, uint8_t *buf, size_t n) {
    return s_dev && i2c_master_transmit_receive(s_dev, &reg, 1, buf, n, 100) == ESP_OK;
}
static bool wr(uint8_t reg, uint8_t val) {
    uint8_t b[2] = { reg, val };
    return s_dev && i2c_master_transmit(s_dev, b, 2, 100) == ESP_OK;
}

static void drop_device(void) {
    if (!s_dev) return;
    i2c_master_bus_rm_device(s_dev);
    s_dev = NULL;
}

static bool try_addr(uint8_t addr) {
    i2c_device_config_t dc = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addr, .scl_speed_hz = 400000 };
    if (i2c_master_bus_add_device(board_i2c_bus(), &dc, &s_dev) != ESP_OK) { s_dev = NULL; return false; }
    uint8_t who = 0;
    if (rd(0x00, &who, 1) && who == 0x05) return true;          // WHO_AM_I
    drop_device();
    return false;
}

bool imu_init(void) {
    if (s_ok) return true;
    int64_t now = esp_timer_get_time();
    if (s_last_try_us > 0 && now - s_last_try_us < IMU_RETRY_US) return false;
    s_last_try_us = now;
    drop_device();
    if (!try_addr(0x6b) && !try_addr(0x6a)) { ESP_LOGW(TAG, "QMI8658 not found"); return false; }
    bool configured =
        wr(0x02, 0x40) &&  // CTRL1:地址自增、小端
        wr(0x03, 0x05) &&  // CTRL2:加速度 ±2g,ODR ~250Hz
        wr(0x04, 0x55) &&  // CTRL3:陀螺仪 ±512dps(64 LSB/dps),ODR ~250Hz —— 数字孪生用
        wr(0x08, 0x03);    // CTRL7:使能加速度计(bit0)+ 陀螺仪(bit1)
    if (!configured) {
        ESP_LOGW(TAG, "QMI8658 configuration failed; retry in 5s");
        drop_device();
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    s_ok = true;
    ESP_LOGI(TAG, "QMI8658 ready");
    return true;
}

bool imu_read_accel(float *x, float *y, float *z) {
    if (!s_ok && !imu_init()) return false;
    uint8_t b[6];
    if (!rd(0x35, b, 6)) return false;
    int16_t ax = (int16_t)((b[1] << 8) | b[0]);
    int16_t ay = (int16_t)((b[3] << 8) | b[2]);
    int16_t az = (int16_t)((b[5] << 8) | b[4]);
    *x = ax / 16384.0f;
    *y = ay / 16384.0f;
    *z = az / 16384.0f;
    return true;
}

bool imu_read_gyro(float *x, float *y, float *z) {
    if (!s_ok && !imu_init()) return false;
    uint8_t b[6];
    if (!rd(0x3B, b, 6)) return false;            // Gx_L..Gz_H,小端 16bit
    int16_t gx = (int16_t)((b[1] << 8) | b[0]);
    int16_t gy = (int16_t)((b[3] << 8) | b[2]);
    int16_t gz = (int16_t)((b[5] << 8) | b[4]);
    *x = gx / 64.0f;                              // ±512dps → 64 LSB/dps
    *y = gy / 64.0f;
    *z = gz / 64.0f;
    return true;
}

bool imu_read_tilt(float *tx, float *ty) {
    float ax, ay, az;
    if (!imu_read_accel(&ax, &ay, &az)) return false;
    *tx = MAP_TX(ax, ay, az);
    *ty = MAP_TY(ax, ay, az);
    return true;
}
