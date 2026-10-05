#pragma once
#include <stdint.h>
#define JPEG_IMAGE_FORMAT_RGB565 1
#define JPEG_IMAGE_SCALE_0 0
typedef struct {
    uint8_t *indata,*outbuf;
    uint32_t indata_size,outbuf_size;
    int out_format,out_scale;
    struct {unsigned swap_color_bytes;} flags;
} esp_jpeg_image_cfg_t;
typedef struct {uint32_t width,height;} esp_jpeg_image_output_t;
int esp_jpeg_decode(esp_jpeg_image_cfg_t *cfg,esp_jpeg_image_output_t *info);
