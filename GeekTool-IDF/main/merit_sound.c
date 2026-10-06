#include "merit_sound.h"
#include <math.h>

void merit_sound_fill(int16_t *out,size_t offset,size_t count) {
    for(size_t i=0;i<count;++i) {
        size_t sample=offset+i;
        if(sample>=MERIT_SOUND_SAMPLES){out[i]=0;continue;}
        float t=sample/(float)MERIT_SOUND_RATE;
        // Inharmonic, differently damped cavity modes avoid the old two-note electronic chirp.
        float wood=.60f*sinf(6.2831853f*740*t)*expf(-t/.032f)
                  +.27f*sinf(6.2831853f*1217*t)*expf(-t/.019f)
                  +.13f*sinf(6.2831853f*2093*t)*expf(-t/.011f)
                  +.07f*sinf(6.2831853f*3461*t)*expf(-t/.006f);
        uint32_t noise=(uint32_t)sample*747796405u+2891336453u;
        noise=((noise>>((noise>>28)+4))^noise)*277803737u;
        noise=(noise>>22)^noise;
        float impact=((noise&65535)/32767.5f-1)*.16f*expf(-t/.002f);
        // Sub-millisecond attack and a final fade keep the PCM endpoints free of discontinuities.
        float attack=fminf(1,sample/8.0f);
        float release=fminf(1,(MERIT_SOUND_SAMPLES-1-sample)/128.0f);
        float value=(wood+impact)*attack*release*24000;
        out[i]=(int16_t)lroundf(fmaxf(-32767,fminf(32767,value)));
    }
}
