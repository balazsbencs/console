#include "magic.h"
#include "tube_pair.h"
#include "air_pressure.h"
#include "iron_transformer.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
struct retro_magic {
    retro_magic_params p;
    float rate, low_coefficient, middle_coefficient, output_coefficient;
    float low[2], middle[2], output[2], mix_filter[7];
    float mix_target, dist_gain, pass_gain;
    unsigned remaining;
    rs_tube_pair tube;
    rs_air_pressure air;
    rs_iron_transformer iron;
};
static float denormal_round(float x) {
    volatile float y=x+9.999999747378752e-05f;
    return y+-9.999999747378752e-05f;
}
static float frequency(float x) { return 1.0f-expf(fminf(x,.49998998641967773f)*-6.2831854820251465f); }
static void setup(retro_magic *s) {
    const retro_magic_params *p=&s->p;
    s->mix_target=p->mix*.01f;
    s->dist_gain=expf((p->dist_db*.05000000074505806f)*2.3025851249694824f);
    s->pass_gain=expf((p->pass_db*.05000000074505806f)*2.3025851249694824f);
    float low=p->low_hz/s->rate, high=p->high_hz/s->rate;
    s->low_coefficient=frequency(low);
    s->middle_coefficient=frequency(fmaxf(high,low));
    s->output_coefficient=frequency(p->output_hz/s->rate);
    float a=p->amount*.01f;
    if (p->character==0) rs_tube_pair_setup(&s->tube,s->rate,a);
    else if (p->character==1) rs_air_pressure_setup(&s->air,s->rate,a);
    else (void)rs_iron_transformer_setup(&s->iron,s->rate,a);
}
retro_magic_params retro_magic_defaults(void) {
    return (retro_magic_params){.amount=50,.low_hz=20,.high_hz=20000,.mix=100,.output_hz=20000};
}
retro_magic *retro_magic_create(void) {
    retro_magic *s=calloc(1,sizeof(*s));
    if(s) s->p=retro_magic_defaults();
    return s;
}
void retro_magic_destroy(retro_magic *s) { if(s) { rs_air_pressure_destroy(&s->air);free(s); } }
int retro_magic_set(retro_magic *s,const retro_magic_params *p) {
    if(!s || !p) return 0;
    if(!isfinite(p->amount) || p->amount<0 || p->amount>100 ||
       !isfinite(p->mix) || p->mix<0 || p->mix>100 ||
       !isfinite(p->character) || p->character<0 || p->character>2 || p->character!=floorf(p->character)) return 0;
    const float hz[]={p->low_hz,p->high_hz,p->output_hz};
    for(unsigned i=0;i<3;++i) if(!isfinite(hz[i]) || hz[i]<20 || hz[i]>20000) return 0;
    const float db[]={p->dist_db,p->pass_db};
    for(unsigned i=0;i<2;++i) if(!isfinite(db[i]) || db[i]<-100 || db[i]>100) return 0;
    if(memcmp(&s->p,p,sizeof(*p))) { s->p=*p;if(s->rate) setup(s); }
    return 1;
}
void retro_magic_reset(retro_magic *s) {
    if(!s) return;
    memset(s->low,0,sizeof(s->low));memset(s->middle,0,sizeof(s->middle));memset(s->output,0,sizeof(s->output));
    memset(s->mix_filter+3,0,4*sizeof(float));s->remaining=64;
    rs_tube_pair_reset(&s->tube);rs_air_pressure_reset(&s->air);rs_iron_transformer_reset(&s->iron);
}
int retro_magic_prepare(retro_magic *s,float rate) {
    if(!s || !isfinite(rate) || rate<1000 || rate>1536000) return 0;
    if(!rs_air_pressure_prepare(&s->air,rate,s->p.amount*.01f)) return 0;
    s->rate=rate;
    float cutoff=fminf(rate*.49900001287460327f,50);
    float angle=(float)(((double)cutoff*-6.2831852)/(double)rate);
    s->mix_filter[0]=(float)exp((double)angle);
    s->mix_filter[1]=(1.0f-s->mix_filter[0])/1.1200000047683716f;
    s->mix_filter[2]=s->mix_filter[1]*.11999999731779099f;
    setup(s);retro_magic_reset(s);return 1;
}
void retro_magic_process(retro_magic *s,float *left,float *right,size_t frames) {
    if(!s || !left || !right || !s->rate) return;
    for(size_t i=0;i<frames;++i) {
        float x[2]={left[i],right[i]}, residual[2], wet[2];
        for(unsigned c=0;c<2;++c) {
            s->low[c]=denormal_round(x[c]-s->low[c])*s->low_coefficient+s->low[c];
            float rest=x[c]-s->low[c];
            s->middle[c]=denormal_round(rest-s->middle[c])*s->middle_coefficient+s->middle[c];
            residual[c]=rest-s->middle[c];wet[c]=s->middle[c];
        }
        if(s->p.character==0) for(unsigned c=0;c<2;++c) wet[c]=rs_tube_pair_process(&s->tube,wet[c],c);
        else if(s->p.character==1) for(unsigned c=0;c<2;++c) wet[c]=rs_air_pressure_process(&s->air,wet[c],c);
        else rs_iron_transformer_process_stereo(&s->iron,&wet[0],&wet[1]);
        for(unsigned c=0;c<2;++c)
            s->output[c]=denormal_round(wet[c]-s->output[c])*s->output_coefficient+s->output[c];
        float *m=s->mix_filter;
        float mix=m[0]*m[3]+(m[2]*m[5]+m[1]*s->mix_target);
        m[3]=mix;m[5]=s->mix_target;
        float passed=s->pass_gain*mix, distorted=mix*s->dist_gain,dry=1.0f-mix;
        float l=s->output[0]*distorted+(residual[0]+s->low[0])*passed;
        float r=s->output[1]*distorted+(residual[1]+s->low[1])*passed;
        left[i]=dry*x[0]+l;right[i]=dry*x[1]+r;
        if(!--s->remaining) {
            for(unsigned c=0;c<2;++c) {s->low[c]=denormal_round(s->low[c]);s->middle[c]=denormal_round(s->middle[c]);s->output[c]=denormal_round(s->output[c]);}
            s->remaining=64;
        }
    }
}
