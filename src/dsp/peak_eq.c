#include "peak_eq.h"
#include <math.h>
#include <string.h>
#include <stddef.h>
_Static_assert(offsetof(rs_peak_eq,batch_left)==160,"native numeric layout");
static float rounded(float x) {volatile float y=x+9.999999747378752e-05f;return y+-9.999999747378752e-05f;}
static void update(rs_peak_eq *s) {
    float frequency=s->frequency/s->rate, q=s->q;
    if(frequency>.49000000953674316f) frequency=.49000000953674316f;
    else if(frequency<4.199999966658652e-05f) {
        q=(.7070000171661377f-q)*.05999999865889549f+q;
        if(frequency>3.125000148429535e-05f)
            q=q+(s->q-q)*((frequency+-3.125000148429535e-05f)/1.0749998182291165e-05f);
        frequency=4.199999966658652e-05f;
    }
    if(s->reset_coefficients) s->interpolation_left=0;
    else {
        s->interpolation_left=(int32_t)(powf(frequency+frequency,.17710000276565552f)*(float)s->maximum_interpolation);
        if(s->interpolation_left) s->inverse_interpolation=1.0f/(float)s->interpolation_left;
    }
    memcpy(s->previous,s->coefficient,sizeof(s->previous));
    unsigned desired=fabsf(s->db)<9.999999747378752e-05f;
    if(desired) {
        if(s->reset_coefficients) s->bypass=1;
        else if(!s->desired_bypass) {s->fade_active=1;s->fade_left=s->fade_frames;s->fade_direction=1;}
    } else {
        s->bypass=0;
        if(s->desired_bypass) {s->fade_active=1;s->fade_left=s->fade_frames;s->fade_direction=0;}
    }
    s->desired_bypass=(uint8_t)desired;
    float angle=frequency*6.2831854820251465f;
    float alpha=(sinf(angle)*.5f)/q;
    float a1=cosf(angle)*-2.0f;
    float amplitude=powf(10.0f,s->db*.02500000037252903f);
    float b0=alpha*amplitude+1.0f;
    float b2=1.0f-alpha*amplitude;
    float reciprocal=1.0f/amplitude;
    reciprocal*=alpha;
    float a2=1.0f-reciprocal;
    float inverse=1.0f/(reciprocal+1.0f);
    s->coefficient[0]=a1*inverse;s->coefficient[1]=inverse*a2;
    s->coefficient[2]=b0*inverse;s->coefficient[3]=a1*inverse;s->coefficient[4]=b2*inverse;
    memcpy(s->target,s->coefficient,sizeof(s->target));
    if(s->reset_coefficients) s->reset_coefficients=0;
}
int rs_peak_eq_set(rs_peak_eq *s,float frequency,float q,float db) {
    if(!s || !isfinite(frequency) || frequency<20 || frequency>20000 ||
        !isfinite(q) || q<.01f || q>10000 || !isfinite(db) || db<-100 || db>100) return 0;
    if(s->frequency==frequency && s->q==q && s->db==db) return 1;
    s->type=4;s->frequency=frequency;s->q=q;s->db=db;
    if(s->rate) update(s);
    return 1;
}
void rs_peak_eq_reset(rs_peak_eq *s) {
    if(!s) return;
    memset(s->history,0,sizeof(s->history));s->reset_coefficients=1;
    memcpy(s->coefficient,s->target,sizeof(s->coefficient));s->interpolation_left=0;
    if(s->fade_active && s->fade_direction==1) s->bypass=1;
    s->fade_active=0;s->fade_left=0;s->batch_left=0;s->lane=0;
}
int rs_peak_eq_prepare(rs_peak_eq *s,float rate) {
    if(!s || !isfinite(rate) || rate<1000 || rate>1536000 || !s->frequency || !s->q) return 0;
    s->type=4;s->rate=rate;memset(s->history,0,sizeof(s->history));s->reset_coefficients=1;
    s->fade_frames=((int32_t)(rate*.009999999776482582f)/64)*64;
    s->inverse_fade=1.0f/(float)s->fade_frames;
    update(s);memcpy(s->coefficient,s->target,sizeof(s->coefficient));
    s->maximum_interpolation=(int32_t)(rate*.07500000298023224f);s->interpolation_left=0;
    if(s->fade_active && s->fade_direction==1) s->bypass=1;
    s->fade_active=0;s->fade_left=0;s->batch_left=0;s->lane=0;return 1;
}
static void begin_batch(rs_peak_eq *s) {
    if(s->interpolation_left) {
        s->interpolation_left-=64;
        if(s->interpolation_left<=0 || s->bypass) {
            if(s->bypass) memset(s->history,0,sizeof(s->history));
            s->interpolation_left=0;memcpy(s->coefficient,s->target,sizeof(s->coefficient));
        } else {
            float t=1.0f-(float)s->interpolation_left*s->inverse_interpolation;
            for(unsigned i=0;i<5;++i) s->coefficient[i]=(s->target[i]-s->previous[i])*t+s->previous[i];
        }
    }
    s->batch_filter=!s->bypass;
    s->batch_fade=s->batch_filter && s->fade_active && s->fade_left>0;
    if(s->batch_fade) {
        int32_t previous=s->fade_left;s->fade_left-=64;
        int32_t end;
        if(s->fade_direction==1) {s->fade_start=(float)previous*s->inverse_fade;end=s->fade_left;}
        else {s->fade_start=(float)(s->fade_frames-previous)*s->inverse_fade;end=64-previous+s->fade_frames;}
        s->fade_end=fminf(fmaxf((float)end*s->inverse_fade,0),1);
    }
    s->batch_left=64;s->lane=0;
}
void rs_peak_eq_process(rs_peak_eq *s,float *left,float *right,size_t frames) {
    if(!s || !left || !right || !s->rate) return;
    for(size_t i=0;i<frames;++i) {
        if(!s->batch_left) begin_batch(s);
        unsigned frame=64-s->batch_left;
        if(s->batch_fade && s->lane==0)
            s->fade_weight=(((float)frame*.015625f)*(s->fade_end-s->fade_start))+s->fade_start;
        if(s->batch_filter) {
            for(unsigned c=0;c<2;++c) {
                float x=c?right[i]:left[i],*h=s->history[c],*a=s->coefficient;
                float sum=h[1]*a[3]+x*a[2];sum=h[0]*a[4]+sum;
                float y=(sum-a[0]*h[3])-h[2]*a[1];
                h[0]=h[1];h[1]=x;h[2]=h[3];h[3]=y;
                float out=y;
                if(s->batch_fade) out=y*s->fade_weight+x*(1.0f-s->fade_weight);
                if(c) right[i]=out;else left[i]=out;
            }
            if(s->lane==3) for(unsigned c=0;c<2;++c) {s->history[c][2]=rounded(s->history[c][2]);s->history[c][3]=rounded(s->history[c][3]);}
        }
        s->lane=(s->lane+1)&3;
        if(!--s->batch_left) {
            if(s->batch_fade && s->fade_left<1) {
                s->fade_active=0;
                if(s->fade_direction==1) {s->bypass=1;memset(s->history,0,sizeof(s->history));}
            }
            for(unsigned c=0;c<2;++c) for(unsigned j=0;j<4;++j)
                if(fabsf(s->history[c][j])<9.999999717180685e-10f) s->history[c][j]=0;
        }
    }
}
