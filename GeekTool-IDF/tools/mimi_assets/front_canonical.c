#include "front_canonical.h"
#include <stdlib.h>
#include <string.h>

typedef struct {uint16_t color;uint8_t q,pad;} pixel_op_t;
static uint32_t hash32(uint32_t n){n^=n>>16;n*=0x7feb352du;n^=n>>15;n*=0x846ca68bu;n^=n>>16;return n;}
static uint16_t apply(const pixel_op_t *ops,unsigned count,uint16_t value,mimi_front_mix_fn mix){
    for(unsigned i=0;i<count;i++)value=mix(ops[i].color,value,ops[i].q);
    return value;
}
void mimi_front_canonical_free(mimi_front_canonical_t *out){if(out){free(out->spans);out->spans=NULL;}}

bool mimi_front_canonicalize(const mimi_asset_span_t *input,const uint32_t rows[467],unsigned frame,
                             const uint16_t *background,const uint16_t *reference,mimi_front_mix_fn mix,
                             mimi_front_canonical_t *out){
    if(!rows||!background||!reference||!mix||!out||rows[0]||(!input&&rows[466]))return false;
    memset(out,0,sizeof *out);
    pixel_op_t *original=malloc(466*MIMI_ASSET_MAX_ROW_SPANS*sizeof *original);
    pixel_op_t *next=malloc(466*MIMI_ASSET_MAX_ROW_SPANS*sizeof *next);
    uint16_t *replay=malloc(466*466*2);
    if(!original||!next||!replay){free(original);free(next);free(replay);return false;}
    memcpy(replay,background,466*466*2);
    pixel_op_t sample[MIMI_ASSET_MAX_ROW_SPANS];uint16_t sample_color=0;size_t capacity=0;bool success=false;
    for(unsigned y=0;y<466;y++){
        if(rows[y]>rows[y+1]||rows[y+1]-rows[y]>MIMI_ASSET_MAX_ROW_SPANS)goto done;
        uint16_t lengths[466]={0},new_lengths[466]={0};unsigned depth=0;out->rows[y]=(uint32_t)out->count;
        for(uint32_t i=rows[y];i<rows[y+1];i++){
            mimi_asset_span_t op=input[i];
            if(!op.n||op.x+op.n>466||!op.q||op.q>32||op.pad)goto done;
            for(unsigned x=op.x;x<op.x+op.n;x++){
                if(lengths[x]>=MIMI_ASSET_MAX_ROW_SPANS)goto done;
                original[x*MIMI_ASSET_MAX_ROW_SPANS+lengths[x]++]=(pixel_op_t){op.color,op.q,0};
            }
        }
        for(unsigned x=0;x<466;x++){
            pixel_op_t *src=original+x*MIMI_ASSET_MAX_ROW_SPANS,*dst=next+x*MIMI_ASSET_MAX_ROW_SPANS;int last=-1;
            for(unsigned i=0;i<lengths[x];i++)if(src[i].q==32)last=(int)i;
            out->old_pixel_ops+=lengths[x];if(lengths[x]>out->old_max_depth)out->old_max_depth=lengths[x];
            if(last>=0){
                uint16_t final=src[last].color;
                for(unsigned i=(unsigned)last+1;i<lengths[x];i++)final=mix(src[i].color,final,src[i].q);
                dst[0]=(pixel_op_t){final,32,0};new_lengths[x]=1;++out->opaque_pixels;
                if(lengths[x]>out->sample_depth){out->sample_depth=lengths[x];memcpy(sample,src,out->sample_depth*sizeof *sample);sample_color=final;}
            }else{
                new_lengths[x]=lengths[x];memcpy(dst,src,lengths[x]*sizeof *src);
                if(memcmp(src,dst,lengths[x]*sizeof *src))goto done;
                if(lengths[x]){++out->transparent_pixels;++out->unchanged_sequences;}
            }
            out->pixel_ops+=new_lengths[x];if(new_lengths[x]>out->max_depth)out->max_depth=new_lengths[x];
            if(new_lengths[x]>depth)depth=new_lengths[x];
            // 任意眼睛/背景属于RGB565域；固定色与随机背景验证每个覆盖像素。
            // Arbitrary eyes/backgrounds lie in the RGB565 domain; fixed and random backgrounds check every covered pixel.
            if(lengths[x])for(unsigned trial=0;trial<8;trial++){
                const uint16_t fixed[]={0,65535,0xf81f,0x07e0};
                uint16_t bg=trial<4?fixed[trial]:(uint16_t)hash32((frame*466u*466u+y*466u+x)*8u+trial);
                if(apply(src,lengths[x],bg,mix)!=apply(dst,new_lengths[x],bg,mix))goto done;
                ++out->background_checks;
            }
        }
        // 按每像素depth分组只交换不同像素的独立操作，保留同一像素自己的顺序。
        // Depth grouping reorders independent pixels only, preserving each pixel's own operation sequence.
        for(unsigned level=0;level<depth;level++)for(unsigned x=0;x<466;){
            if(new_lengths[x]<=level){++x;continue;}
            pixel_op_t op=next[x*MIMI_ASSET_MAX_ROW_SPANS+level];unsigned first=x++;
            while(x<466&&new_lengths[x]>level){pixel_op_t n=next[x*MIMI_ASSET_MAX_ROW_SPANS+level];if(n.q!=op.q||n.color!=op.color)break;++x;}
            if(out->count==capacity){
                size_t enlarged=capacity?capacity*2:4096;void *memory=realloc(out->spans,enlarged*sizeof *out->spans);
                if(!memory)goto done;
                out->spans=memory;capacity=enlarged;
            }
            out->spans[out->count++]=(mimi_asset_span_t){(uint16_t)first,(uint16_t)(x-first),op.color,op.q,0};
            for(unsigned px=first;px<x;px++)replay[y*466+px]=mix(op.color,replay[y*466+px],op.q);
        }
        unsigned row_count=(unsigned)(out->count-out->rows[y]);if(row_count>out->max_row)out->max_row=row_count;
    }
    out->rows[466]=(uint32_t)out->count;
    if(memcmp(replay,reference,466*466*2))goto done;
    // 每帧最长的不透明栈穷举全部背景，作为数学恒等式之外的独立穷举防线。
    // Exhaust every background for the longest opaque stack of each frame, in addition to the mathematical identity.
    if(out->sample_depth)for(unsigned bg=0;bg<65536;bg++){
        if(apply(sample,out->sample_depth,(uint16_t)bg,mix)!=sample_color||mix(sample_color,(uint16_t)bg,32)!=sample_color)goto done;
        ++out->exhaustive_background_checks;
    }
    success=true;
done:
    free(original);free(next);free(replay);
    if(!success)mimi_front_canonical_free(out);
    return success;
}
