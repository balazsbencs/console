#include "classic.h"
#include "classic_split.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
struct retro_classic {
    retro_classic_params p;
    float rate;
    rs_classic_split_state split;
    float dry_gain, dist_gain, pass_gain, reference_gain;
    float drive, bias, normalization, offset;
    float output_coefficient, output[2];
    unsigned remaining;
};
static float rounded(float x) {
    volatile float y=x+9.999999747378752e-05f;
    return y+-9.999999747378752e-05f;
}
static float frequency(float x) {
    return 1.0f-expf(fminf(x,.49998998641967773f)*-6.2831854820251465f);
}
static void setup(retro_classic *s) {
    const retro_classic_params *p=&s->p;
    s->dry_gain=1.0f-p->mix*.01f;
    s->dist_gain=(expf((p->dist_db*.05f)*2.3025851249694824f)*p->mix)*.01f;
    s->pass_gain=(expf((p->pass_db*.05f)*2.3025851249694824f)*p->mix)*.01f;
    s->reference_gain=expf((p->reference_db*-.05f)*2.3025851249694824f);
    float low=p->low_hz/s->rate, high=p->high_hz/s->rate;
    s->split.low_coefficient=frequency(low);
    s->split.middle_coefficient=frequency(fmaxf(low,high));
    s->output_coefficient=frequency(p->output_hz/s->rate);
    if(p->character==0) {
        s->drive=expf((p->amount*.01f)*3.9120230674743652f)*.5f;
        float n=1;
        if(s->drive>=1) n=sqrtf(s->drive/(s->drive*3.0f+-2.0f));
        s->normalization=n/s->reference_gain;
    } else if(p->character==1) {
        float d=expf((p->amount*-.012999999336898327f+0.0f)*4.605170249938965f);
        s->drive=d;s->bias=d/3.0f;
        s->normalization=((((d+1.0f)*1.7320507764816284f)*(d+1.0f))/(d+3.0f))/s->reference_gain;
        s->offset=(s->normalization*s->bias)/(s->bias+d);
    } else {
        s->drive=(expf((p->amount*.01f)*2.4849066734313965f)*.20000000298023224f)*s->reference_gain;
        s->normalization=.7070000171661377f/s->reference_gain;
    }
}
retro_classic_params retro_classic_defaults(void) {
    return (retro_classic_params){.amount=50,.low_hz=20,.high_hz=20000,.mix=100,.output_hz=20000};
}
retro_classic *retro_classic_create(void) {
    retro_classic *s=calloc(1,sizeof(*s));
    if(s) s->p=retro_classic_defaults();
    return s;
}
void retro_classic_destroy(retro_classic *s) { free(s); }
int retro_classic_set(retro_classic *s,const retro_classic_params *p) {
    if(!s || !p || !isfinite(p->amount) || p->amount<0 || p->amount>100 ||
       !isfinite(p->mix) || p->mix<0 || p->mix>100 ||
       !isfinite(p->character) || p->character<0 || p->character>2 || p->character!=floorf(p->character)) return 0;
    const float hz[]={p->low_hz,p->high_hz,p->output_hz};
    for(unsigned i=0;i<3;++i) if(!isfinite(hz[i]) || hz[i]<20 || hz[i]>20000) return 0;
    const float db[]={p->dist_db,p->pass_db,p->reference_db};
    for(unsigned i=0;i<3;++i) if(!isfinite(db[i]) || db[i]<-100 || db[i]>100) return 0;
    if(memcmp(&s->p,p,sizeof(*p))) {s->p=*p;if(s->rate) setup(s);}
    return 1;
}
void retro_classic_reset(retro_classic *s) {
    if(!s) return;
    memset(s->split.history,0,sizeof(s->split.history));memset(s->output,0,sizeof(s->output));s->remaining=64;
}
int retro_classic_prepare(retro_classic *s,float rate) {
    if(!s || !isfinite(rate) || rate<1000 || rate>1536000) return 0;
    s->rate=rate;setup(s);retro_classic_reset(s);return 1;
}
static float shape(const retro_classic *s,float x) {
    if(s->p.character==0) {
        x*=s->reference_gain;
        float z=1.0f-fabsf(x);z*=z;z*=s->drive;z+=1.0f;z*=x;
        return fminf(fmaxf(z,-1.0f),1.0f)*s->normalization;
    }
    if(s->p.character==1) {
        x=x*s->reference_gain+s->bias;
        float numerator=s->normalization*x, denominator=fabsf(x)+s->drive;
#if defined(__SSE__)
        float r=_mm_cvtss_f32(_mm_rcp_ss(_mm_set_ss(denominator)));
        r=(r+r)-((denominator*r)*r);
        return r*numerator-s->offset;
#else
        /* SSE reciprocal estimates are CPU-specific. Portable division retains
           the recovered transfer function; ARM bit identity is unverified. */
        return numerator/denominator-s->offset;
#endif
    }
    x=x*s->drive+.25f;
    /* Match CVTTPS2DQ's indefinite result without an out-of-range C cast. */
    int32_t integer=INT32_MIN;
    if(isfinite(x) && x>=-2147483648.0f && x<2147483648.0f) integer=(int32_t)x;
    float q=fabsf(x-(float)integer)+-.5f;
    float square=q*q;
    float z=fabsf(q*32.0f)+-24.0f;z*=square;z+=1.0f;
    return z*s->normalization;
}
void retro_classic_process(retro_classic *s,float *left,float *right,size_t frames) {
    if(!s || !left || !right || !s->rate) return;
    for(size_t i=0;i<frames;++i) {
        float x[2]={left[i],right[i]}, low[2],middle[2],high[2];
        rs_classic_split_sample(&s->split,x[0],x[1],low,middle,high);
        for(unsigned c=0;c<2;++c) {
            float wet=(shape(s,middle[c])-s->output[c])*s->output_coefficient+s->output[c];
            s->output[c]=rounded(wet);
            float out=(low[c]+high[c])*s->pass_gain+(wet*s->dist_gain+x[c]*s->dry_gain);
            if(c) right[i]=out;else left[i]=out;
        }
        if(!--s->remaining) {
            for(unsigned c=0;c<2;++c) for(unsigned h=0;h<2;++h) s->split.history[c][h]=rounded(s->split.history[c][h]);
            s->remaining=64;
        }
    }
}
