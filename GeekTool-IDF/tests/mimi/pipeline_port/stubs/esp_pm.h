#pragma once
typedef int esp_err_t;
typedef struct test_power_lock *esp_pm_lock_handle_t;
#define ESP_OK 0
#define ESP_FAIL (-1)
#define ESP_PM_CPU_FREQ_MAX 1
esp_err_t esp_pm_lock_create(int,int,const char *,esp_pm_lock_handle_t *);
esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t);
esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t);
esp_err_t esp_pm_lock_delete(esp_pm_lock_handle_t);
