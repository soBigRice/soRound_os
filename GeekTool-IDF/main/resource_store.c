#include "resource_store.h"
#include "esp_vfs_fat.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "resources";
static portMUX_TYPE s_init_mux = portMUX_INITIALIZER_UNLOCKED;
static StaticSemaphore_t s_mount_storage;
static SemaphoreHandle_t s_mount_mutex;
static bool s_mounted;

bool resource_store_mount(void) {
    // 自旋临界区仅建立静态锁；Flash/FAT挂载和等待必须在临界区之外。
    // The spinlock only initializes the static mutex; Flash/FAT mounting and waits stay outside it.
    portENTER_CRITICAL(&s_init_mux);
    if(!s_mount_mutex)s_mount_mutex=xSemaphoreCreateMutexStatic(&s_mount_storage);
    SemaphoreHandle_t mutex=s_mount_mutex;
    portEXIT_CRITICAL(&s_init_mux);
    if(!mutex||xSemaphoreTake(mutex,pdMS_TO_TICKS(2000))!=pdTRUE)return false;
    if(!s_mounted) {
        const esp_vfs_fat_mount_config_t config={.max_files=2,.format_if_mount_failed=false};
        esp_err_t error=esp_vfs_fat_spiflash_mount_ro("/img","storage",&config);
        if(error==ESP_OK)s_mounted=true;
        else ESP_LOGW(TAG,"FAT mount(ro) failed: %s",esp_err_to_name(error));
    }
    bool mounted=s_mounted;
    xSemaphoreGive(mutex);
    return mounted;
}
