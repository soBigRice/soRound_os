// 图片表盘存储 —— FAT(只读)挂载 + JPEG 解码到 PSRAM。见 img_store.h。
// 内部 RAM 紧张(显存已降到 80 行),故输入/输出缓冲都放 PSRAM;解码用 espressif/esp_jpeg(tjpgd,仅基线 JPEG)。
#include "img_store.h"
#include "watchface_backgrounds.h"
#include "esp_vfs_fat.h"
#include "esp_heap_caps.h"
#include "esp_rom_crc.h"
#include "esp_log.h"
#include "jpeg_decoder.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "img";

#define IMG_W     466
#define IMG_H     466
#define IMG_PATH  "/img/bg.jpg"
#define IMG_MAX   (2 * 1024 * 1024)   // 输入 JPEG 上限
// esp_jpeg(本板用 ROM TJpgDec,JD_FORMAT=0)以 swap=0 输出小端 RGB565,正好是 LVGL 原生格式;
// 屏端口的 swap_bytes=true 是统一的下游处理(UI 也走它),所以这里必须 0。设 1 会得到"法线贴图"般的乱色。
#define IMG_SWAP  0

static lv_image_dsc_t s_dsc;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_started, s_done, s_ok;              // 只解码一次,之后返回缓存

static bool mount_fat(void) {
    static bool mounted = false;
    if (mounted) return true;
    esp_vfs_fat_mount_config_t mc = { .max_files = 2, .format_if_mount_failed = false };
    esp_err_t e = esp_vfs_fat_spiflash_mount_ro("/img", "storage", &mc);
    if (e != ESP_OK) { ESP_LOGW(TAG, "FAT mount(ro) failed: %s", esp_err_to_name(e)); return false; }
    mounted = true;
    return true;
}

static const lv_image_dsc_t *decode_image(void) {
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

    // OTA leaves the read-only FAT partition intact. The original factory panda
    // is a default, not a user override; identify its contents, never its filename alone.
    // CRC is only an asset fingerprint here, not a trust or integrity decision.
    if (sz == 22494 && esp_rom_crc32_le(0, in, (uint32_t)sz) == 0x8734e6d4u) {
        free(in);
        ESP_LOGI(TAG, "legacy factory background: using selected theme");
        return NULL;
    }

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
    return &s_dsc;
}

static void decode_task(void *arg) {
    (void)arg;
    bool ok = decode_image() != NULL;
    portENTER_CRITICAL(&s_mux); s_ok = ok; s_done = true; portEXIT_CRITICAL(&s_mux);
    vTaskDelete(NULL);
}
const lv_image_dsc_t *img_store_face_image(void) {
    portENTER_CRITICAL(&s_mux);
    bool done = s_done, ok = s_ok, start = !s_started;
    s_started = true;
    portEXIT_CRITICAL(&s_mux);
    if (start && xTaskCreate(decode_task, "image_decode", 4096, NULL, 2, NULL) != pdPASS) {
        ESP_LOGW(TAG, "image decode task allocation failed");
        portENTER_CRITICAL(&s_mux); s_done = true; s_ok = false; portEXIT_CRITICAL(&s_mux);
    }
    return done && ok ? &s_dsc : NULL;
}
bool img_store_loading(void) {
    portENTER_CRITICAL(&s_mux); bool loading = s_started && !s_done; portEXIT_CRITICAL(&s_mux);
    return loading;
}

static lv_image_dsc_t s_defaults[3];
static bool s_default_started[3],s_default_done[3],s_default_ok[3];

static void default_decode_task(void *arg) {
    int theme=(int)(intptr_t)arg;
    size_t size=(size_t)IMG_W*IMG_H*2;
    uint8_t *out=heap_caps_malloc(size,MALLOC_CAP_SPIRAM);
    bool ok=false;
    if(out) {
        esp_jpeg_image_cfg_t cfg={.indata=(uint8_t *)watchface_backgrounds[theme].data,
            .indata_size=(uint32_t)watchface_backgrounds[theme].size,.outbuf=out,.outbuf_size=(uint32_t)size,
            .out_format=JPEG_IMAGE_FORMAT_RGB565,.out_scale=JPEG_IMAGE_SCALE_0,.flags={.swap_color_bytes=IMG_SWAP}};
        esp_jpeg_image_output_t info={0};
        ok=esp_jpeg_decode(&cfg,&info)==ESP_OK&&info.width==IMG_W&&info.height==IMG_H;
        if(ok)s_defaults[theme]=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,
            .w=IMG_W,.h=IMG_H,.stride=IMG_W*2},.data=out,.data_size=size};
        else free(out);
    }
    portENTER_CRITICAL(&s_mux);s_default_ok[theme]=ok;s_default_done[theme]=true;portEXIT_CRITICAL(&s_mux);
    vTaskDelete(NULL);
}
const lv_image_dsc_t *img_store_face_image_for(int theme) {
    if(theme<0||theme>=3)return NULL;
    const lv_image_dsc_t *custom=img_store_face_image();
    bool loading=img_store_loading();
    if(!custom&&!loading)custom=img_store_face_image(); // Decoder may complete between these reads.
    if(custom||loading)return custom;
    portENTER_CRITICAL(&s_mux);
    bool done=s_default_done[theme],ok=s_default_ok[theme],start=!s_default_started[theme];
    s_default_started[theme]=true;portEXIT_CRITICAL(&s_mux);
    if(start&&xTaskCreate(default_decode_task,"face_background",4096,(void *)(intptr_t)theme,2,NULL)!=pdPASS) {
        ESP_LOGW(TAG,"default background task allocation failed");
        portENTER_CRITICAL(&s_mux);s_default_done[theme]=true;portEXIT_CRITICAL(&s_mux);
    }
    return done&&ok?&s_defaults[theme]:NULL;
}
bool img_store_face_loading(int theme) {
    if(theme<0||theme>=3)return false;
    portENTER_CRITICAL(&s_mux);
    bool loading=(s_started&&!s_done)||(!s_ok&&s_default_started[theme]&&!s_default_done[theme]);
    portEXIT_CRITICAL(&s_mux);return loading;
}
