#pragma once
#include "sdk.h"
typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;
typedef struct {int dev_addr_length;uint8_t device_address;int scl_speed_hz;} i2c_device_config_t;
#define I2C_ADDR_BIT_LEN_7 0
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t,const i2c_device_config_t *,i2c_master_dev_handle_t *);
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t);
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t,const uint8_t *,size_t,uint8_t *,size_t,int);
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t,const uint8_t *,size_t,int);
