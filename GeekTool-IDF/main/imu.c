// QMI8658 加速度计 —— 见 imu.h。寄存器:WHO_AM_I(0x00)=0x05;CTRL1(0x02)/CTRL2(0x03)/CTRL7(0x08);
// 加速度输出 0x35..0x3A(Ax_L..Az_H,小端 16bit)。±2g → 16384 LSB/g。
#include "imu.h"
#include "board_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "imu";
static i2c_master_dev_handle_t s_dev;
static bool s_ok, s_initializing;
static portMUX_TYPE s_init_mux = portMUX_INITIALIZER_UNLOCKED;
static int64_t s_ready_at_us;
static int64_t s_retry_after_us, s_sample_changed_us;
static uint32_t s_sample_stamp;
static bool s_have_stamp;
static unsigned s_read_errors;
static SemaphoreHandle_t s_io_lock;
static StaticSemaphore_t s_io_storage;

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

static bool init_device(void) {
    int64_t now = esp_timer_get_time();
    if (now < s_retry_after_us) return false;
    s_retry_after_us = now + IMU_RETRY_US;
    drop_device();
    if (!try_addr(0x6b) && !try_addr(0x6a)) { ESP_LOGW(TAG, "QMI8658 not found"); return false; }
    bool configured =
        wr(0x08, 0x00) &&  // Stop sampling before changing configuration/recovery.
        wr(0x02, 0x40) &&  // CTRL1:地址自增、小端
        wr(0x03, 0x05) &&  // CTRL2:加速度 ±2g,ODR ~250Hz
        wr(0x04, 0x55) &&  // CTRL3:陀螺仪 ±512dps(64 LSB/dps),ODR ~250Hz —— 数字孪生用
        wr(0x08, 0x03);    // CTRL7:使能加速度计(bit0)+ 陀螺仪(bit1)
    if (!configured) {
        ESP_LOGW(TAG, "QMI8658 configuration failed; retry in 5s");
        drop_device();
        return false;
    }
    s_ready_at_us = esp_timer_get_time() + 20000;
    s_read_errors = 0; s_have_stamp = false;
    ESP_LOGI(TAG, "QMI8658 ready");
    return true;
}

bool imu_init(void) {
    portENTER_CRITICAL(&s_init_mux);
    bool ready = s_ok, busy = s_initializing;
    if (!ready && !busy) s_initializing = true;
    portEXIT_CRITICAL(&s_init_mux);
    if (ready || busy) return ready;
    if (!s_io_lock) s_io_lock = xSemaphoreCreateMutexStatic(&s_io_storage);
    bool ok = false;
    if (s_io_lock && xSemaphoreTake(s_io_lock, pdMS_TO_TICKS(120)) == pdTRUE) {
        ok = init_device();
        xSemaphoreGive(s_io_lock);
    }
    portENTER_CRITICAL(&s_init_mux); s_ok = ok; s_initializing = false; portEXIT_CRITICAL(&s_init_mux);
    return ok;
}

// The IO mutex serializes background twin sampling with foreground apps and
// recovery. It never resets the shared touch/PMU/audio I2C bus.
static void sample_fault(const char *reason, int64_t now) {
    ESP_LOGW(TAG, "sensor stalled (%s); reconfigure in 1s", reason);
    portENTER_CRITICAL(&s_init_mux); s_ok = false; portEXIT_CRITICAL(&s_init_mux);
    s_retry_after_us = now + 1000000;
}
static bool read_sample(uint8_t *b, size_t count) {
    if (!imu_init() || !s_io_lock) return false;
    if (xSemaphoreTake(s_io_lock, pdMS_TO_TICKS(120)) != pdTRUE) return false;
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_init_mux); bool ready=s_ok; portEXIT_CRITICAL(&s_init_mux);
    bool ok=false;
    if (ready && now >= s_ready_at_us) {
        // QST Rev A section 5.5: 0x30..32 increments every sample, even when
        // stationary. Checking acceleration equality would falsely flag rest.
        if (!rd(0x30,b,count)) {
            if (++s_read_errors >= 3) sample_fault("I2C read",now);
        } else {
            s_read_errors=0;
            uint32_t stamp=(uint32_t)b[0] | ((uint32_t)b[1]<<8) | ((uint32_t)b[2]<<16);
            if (!s_have_stamp || stamp != s_sample_stamp) {
                s_have_stamp=true; s_sample_stamp=stamp; s_sample_changed_us=now; ok=true;
            } else if (now-s_sample_changed_us >= 1000000) sample_fault("sample timestamp",now);
            else ok=true; // Faster callers may read the same valid frame.
        }
    }
    xSemaphoreGive(s_io_lock); return ok;
}

bool imu_read_accel(float *x, float *y, float *z) {
    uint8_t b[11];
    if (!read_sample(b,sizeof b)) return false;
    int16_t ax = (int16_t)((b[6] << 8) | b[5]);
    int16_t ay = (int16_t)((b[8] << 8) | b[7]);
    int16_t az = (int16_t)((b[10] << 8) | b[9]);
    *x = ax / 16384.0f;
    *y = ay / 16384.0f;
    *z = az / 16384.0f;
    return true;
}

bool imu_read_gyro(float *x, float *y, float *z) {
    uint8_t b[17];
    if (!read_sample(b,sizeof b)) return false;
    int16_t gx = (int16_t)((b[12] << 8) | b[11]);
    int16_t gy = (int16_t)((b[14] << 8) | b[13]);
    int16_t gz = (int16_t)((b[16] << 8) | b[15]);
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

bool imu_read_tilt_z(float *tx, float *ty, float *z) {
    float ax, ay, az;
    if (!imu_read_accel(&ax, &ay, &az)) return false;
    *tx = MAP_TX(ax, ay, az); *ty = MAP_TY(ax, ay, az); *z = az;
    return true;
}
