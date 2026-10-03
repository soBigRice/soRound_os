#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "sdk.h"
#include "board_config.h"
#include "freertos/semphr.h"
static int64_t now;
static uint8_t regs[128];
static bool fail_reads,fail_write;
static int additions,removals;
static void log_message(const char *fmt,...) {(void)fmt;}
#undef ESP_LOGI
#undef ESP_LOGW
#define ESP_LOGI(tag,...) ((void)(tag),log_message(__VA_ARGS__))
#define ESP_LOGW(tag,...) ((void)(tag),log_message(__VA_ARGS__))
int64_t esp_timer_get_time(void) {return now;}
i2c_master_bus_handle_t board_i2c_bus(void) {return (void *)1;}
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus,const i2c_device_config_t *cfg,i2c_master_dev_handle_t *d) {
    assert(bus && cfg->device_address==0x6b);*d=(void *)2;++additions;return ESP_OK;
}
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t d) {assert(d);++removals;return ESP_OK;}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t d,const uint8_t *r,size_t rs,uint8_t *out,size_t n,int timeout) {
    assert(d && rs==1 && timeout==100);if(fail_reads && *r>=0x30)return ESP_FAIL;
    memcpy(out,regs+*r,n);return ESP_OK;
}
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t d,const uint8_t *data,size_t n,int timeout) {
    assert(d && n==2 && timeout==100);if(fail_write)return ESP_FAIL;regs[data[0]]=data[1];return ESP_OK;
}
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *storage) {storage->held=false;return storage;}
int xSemaphoreTake(SemaphoreHandle_t s,int timeout) {assert(!s->held && timeout==120);s->held=true;return pdTRUE;}
void xSemaphoreGive(SemaphoreHandle_t s) {assert(s->held);s->held=false;}
#include "../../main/imu.c"
static void sample(uint32_t stamp,int16_t x,int16_t y,int16_t z) {
    regs[0x30]=stamp;regs[0x31]=stamp>>8;regs[0x32]=stamp>>16;
    int16_t values[]={x,y,z,64,-128,192};
    for(unsigned i=0;i<6;++i) {regs[0x35+i*2]=(uint16_t)values[i]&255;regs[0x36+i*2]=(uint16_t)values[i]>>8;}
}
int main(void) {
    regs[0]=5;now=100000;assert(imu_init());assert(additions==1);
    float x=99,y=99,z=99;sample(1,8192,-16384,0);
    assert(!imu_read_tilt_z(&x,&y,&z));assert(x==99);
    now+=21000;assert(imu_read_tilt_z(&x,&y,&z));assert(x==-1 && y==.5f && z==0);
    assert(imu_read_gyro(&x,&y,&z) && x==1 && y==-2 && z==3);
    // Every side of the display maps correctly, including a signed -1g edge.
    const int16_t sides[4][3]={{0,16384,0},{0,-16384,0},{16384,0,0},{-16384,0,0}};
    for(unsigned i=0;i<4;++i) {now+=20000;sample(i+2,sides[i][0],sides[i][1],sides[i][2]);assert(imu_read_tilt_z(&x,&y,&z));assert(x==sides[i][1]/16384.f && y==sides[i][0]/16384.f);}
    // Stationary values remain healthy as the sample counter advances, including wrap.
    now+=20000;sample(0xffffff,0,0,16384);assert(imu_read_accel(&x,&y,&z));
    now+=20000;sample(0,0,0,16384);assert(imu_read_accel(&x,&y,&z));
    for(unsigned i=0;i<100;++i) {now+=20000;sample(i+1,0,0,16384);assert(imu_read_accel(&x,&y,&z));}
    assert(additions==1);
    fail_reads=true;for(int i=0;i<3;++i) {now+=20000;assert(!imu_read_accel(&x,&y,&z));}
    assert(!s_ok);fail_reads=false;assert(!imu_read_accel(&x,&y,&z));assert(additions==1);
    now+=1000001;assert(!imu_read_accel(&x,&y,&z));assert(additions==2 && removals==1);
    now+=21000;sample(200,0,16384,0);assert(imu_read_accel(&x,&y,&z));
    now+=1000001;assert(!imu_read_accel(&x,&y,&z) && !s_ok); // A frozen sample counter is a fault.
    fail_write=true;now+=1000001;assert(!imu_read_accel(&x,&y,&z));int attempts=additions;
    now+=100000;assert(!imu_read_accel(&x,&y,&z) && additions==attempts);
    fail_write=false;now+=5000000;assert(!imu_read_accel(&x,&y,&z));now+=21000;sample(300,0,0,16384);
    assert(imu_read_accel(&x,&y,&z) && z==1);
    puts("IMU signed axes/gyro, warmup, timestamp wrap/rest, I2C failures, stalled samples, reconfiguration and bounded retries passed");
}
