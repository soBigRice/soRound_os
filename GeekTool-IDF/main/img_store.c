// 图片表盘存储 —— FAT(只读)挂载 + JPEG 解码到 PSRAM。见 img_store.h。
// 内部 RAM 紧张(显存已降到 80 行),故输入/输出缓冲都放 PSRAM;解码用 espressif/esp_jpeg(tjpgd,仅基线 JPEG)。
#include "img_store.h"
#include "esp_vfs_fat.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "jpeg_decoder.h"
#include <stdio.h>

static const char *TAG = "img";

#define IMG_W     466
#define IMG_H     466
#define IMG_PATH  "/img/bg.jpg"
#define IMG_MAX   (2 * 1024 * 1024)   // 输入 JPEG 上限
// esp_jpeg(本板用 ROM TJpgDec,JD_FORMAT=0)以 swap=0 输出小端 RGB565,正好是 LVGL 原生格式;
// 屏端口的 swap_bytes=true 是统一的下游处理(UI 也走它),所以这里必须 0。设 1 会得到"法线贴图"般的乱色。
#define IMG_SWAP  0

static lv_image_dsc_t s_dsc;
static bool s_done, s_ok;              // 只解码一次,之后返回缓存

static bool mount_fat(void) {
    static bool mounted = false;
    if (mounted) return true;
    esp_vfs_fat_mount_config_t mc = { .max_files = 2, .format_if_mount_failed = false };
    esp_err_t e = esp_vfs_fat_spiflash_mount_ro("/img", "storage", &mc);
    if (e != ESP_OK) { ESP_LOGW(TAG, "FAT mount(ro) failed: %s", esp_err_to_name(e)); return false; }
    mounted = true;
    return true;
}

const lv_image_dsc_t *img_store_face_image(void) {
    if (s_done) return s_ok ? &s_dsc : NULL;
    s_done = true;
    if (!mount_fat()) return NULL;

    FILE *f = fopen(IMG_PATH, "rb");
    if (!f) { ESP_LOGW(TAG, "open %s failed (no image yet?)", IMG_PATH); return NULL; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > IMG_MAX) { fclose(f); ESP_LOGW(TAG, "bad jpg size %ld", sz); return NULL; }

    uint8_t *in = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    if (!in) { fclose(f); ESP_LOGW(TAG, "no PSRAM for input"); return NULL; }
    size_t rd = fread(in, 1, sz, f);
    fclose(f);
    if (rd != (size_t)sz) { free(in); return NULL; }

    size_t outsz = (size_t)IMG_W * IMG_H * 2;
    uint8_t *out = heap_caps_malloc(outsz, MALLOC_CAP_SPIRAM);
    if (!out) { free(in); ESP_LOGW(TAG, "no PSRAM for output"); return NULL; }

    esp_jpeg_image_cfg_t cfg = {
        .indata      = in,
        .indata_size = (uint32_t)sz,
        .outbuf      = out,
        .outbuf_size = (uint32_t)outsz,
        .out_format  = JPEG_IMAGE_FORMAT_RGB565,
        .out_scale   = JPEG_IMAGE_SCALE_0,
        .flags       = { .swap_color_bytes = IMG_SWAP },
    };
    esp_jpeg_image_output_t info = { 0 };
    esp_err_t e = esp_jpeg_decode(&cfg, &info);
    free(in);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "jpeg decode failed: %s (need baseline %dx%d JPEG)", esp_err_to_name(e), IMG_W, IMG_H);
        free(out);
        return NULL;
    }
    ESP_LOGI(TAG, "decoded %dx%d", info.width, info.height);

    s_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    s_dsc.header.cf     = LV_COLOR_FORMAT_RGB565;
    s_dsc.header.w      = info.width  ? info.width  : IMG_W;
    s_dsc.header.h      = info.height ? info.height : IMG_H;
    s_dsc.header.stride = s_dsc.header.w * 2;
    s_dsc.data          = out;
    s_dsc.data_size     = outsz;
    s_ok = true;
    return &s_dsc;
}
