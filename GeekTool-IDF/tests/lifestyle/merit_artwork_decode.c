// Decode the actual embedded JPEG with the project's Tiny JPEG implementation on the host.
#include "jpeg_decoder.h"
#include "esp_heap_caps.h"
#include "tjpgd.h"
#include <stdlib.h>
#include <string.h>

typedef struct {esp_jpeg_image_cfg_t *cfg;size_t offset;} input_t;
static size_t read_jpeg(JDEC *decoder,uint8_t *buffer,size_t count) {
    input_t *in=decoder->device;
    size_t left=in->cfg->indata_size-in->offset;
    if(count>left)count=left;
    if(buffer)memcpy(buffer,in->cfg->indata+in->offset,count);
    in->offset+=count;return count;
}
static int write_rgb565(JDEC *decoder,void *bitmap,JRECT *rect) {
    input_t *in=decoder->device;uint8_t *rgb=bitmap;
    for(unsigned y=rect->top;y<=rect->bottom;++y)for(unsigned x=rect->left;x<=rect->right;++x) {
        size_t at=((size_t)y*decoder->width+x)*2;
        if(at+1>=in->cfg->outbuf_size)return 0;
        uint16_t p=(uint16_t)(((rgb[0]>>3)<<11)|((rgb[1]>>2)<<5)|(rgb[2]>>3));rgb+=3;
        in->cfg->outbuf[at]=(uint8_t)p;in->cfg->outbuf[at+1]=(uint8_t)(p>>8);
    }
    return 1;
}
int esp_jpeg_decode(esp_jpeg_image_cfg_t *cfg,esp_jpeg_image_output_t *info) {
    JDEC decoder;input_t in={cfg,0};_Alignas(8) uint8_t work[4096];
    if(jd_prepare(&decoder,read_jpeg,work,sizeof work,&in)!=JDR_OK)return -1;
    if((size_t)decoder.width*decoder.height*2>cfg->outbuf_size)return -1;
    info->width=decoder.width;info->height=decoder.height;
    return jd_decomp(&decoder,write_rgb565,0)==JDR_OK?0:-1;
}
