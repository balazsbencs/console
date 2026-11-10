#include "magnetic.h"
#include "curves.h"
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float value; bool initial; } smoother;
struct retro_magnetic {
    retro_magnetic_params p;
    float inverse_rate, max_step, feed, phase[2], increment;
    smoother smooth[3];
    bool started, stereo_changed, skip;
    unsigned remaining;
    float old[8], target[8], delta[8], depth_gate, gain_boost;
};
static float clamp(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
/* Same recovered cLFO::Sine polynomial used by Wobble. */
static float sine(float phase) {
    float x; bool negative = phase >= 0.5f;
    if (phase < 0.5f) x = phase < 0.25f ? 0.25f - phase : phase + -0.25f;
    else x = phase < 0.75f ? 0.75f - phase : phase + -0.75f;
    x *= x;
    float y = (57.34150695800781f * x + -19.564743041992188f) * x + 0.9988061785697937f;
    return y * (negative ? -1.0011951923370361f : 1.0011951923370361f);
}
static float shaped(float value) {
    float x = fabsf(value);
    if (x < -0.1f) return 0;
    if (x > 1.1f) return 1;
    x = (x + 0.1f) / 1.2f;
    return (3.0f - (x + x)) * (x * x);
}
static float smooth_value(retro_magnetic *s, unsigned index, float value) {
    smoother *v = &s->smooth[index];
    value *= 16384.0f;
    if (v->initial) { v->initial = false; v->value = value; }
    else if (v->value != value) {
        float d = (value - v->value) * s->feed;
        float sign = d < 0 ? -1.0f : 1.0f;
        float distance = d * sign;
        v->value = distance < 0.016383999958634377f ? value :
            v->value + sign * fminf(s->max_step, distance);
    }
    return v->value * 6.103515625e-05f;
}
retro_magnetic_params retro_magnetic_defaults(void) {
    return (retro_magnetic_params){.depth=1, .depth_steer=.5f, .rate=.5f,
        .wear_depth=1, .dropout_min=.1f, .dropout_max=.25f, .enable=1};
}
retro_magnetic *retro_magnetic_create(void) {
    retro_magnetic *s = calloc(1, sizeof(*s));
    if (s) s->p = retro_magnetic_defaults();
    return s;
}
void retro_magnetic_destroy(retro_magnetic *s) { free(s); }
int retro_magnetic_set(retro_magnetic *s, const retro_magnetic_params *p) {
    if (!s || !p) return 0;
    const float unit[] = {p->depth, p->rate, p->stereo_phase, p->wear_depth,
        p->dropout_steer, p->dropout_amount, p->dropout_min, p->dropout_max};
    for (unsigned i=0; i<sizeof(unit)/sizeof(*unit); ++i)
        if (!isfinite(unit[i]) || unit[i]<0 || unit[i]>1) return 0;
    const float bipolar[] = {p->depth_steer,p->wear_gain,p->wear_pan};
    for (unsigned i=0; i<sizeof(bipolar)/sizeof(*bipolar); ++i)
        if (!isfinite(bipolar[i]) || bipolar[i]<-1 || bipolar[i]>1) return 0;
    if ((p->enable!=0 && p->enable!=1) || (p->wear_stereo!=0 && p->wear_stereo!=1) ||
        p->dropout_max <= p->dropout_min) return 0;
    if (memcmp(&s->p,p,sizeof(*p))) {
        if (s->p.stereo_phase != p->stereo_phase) s->stereo_changed=true;
        s->p=*p; s->remaining=0;
    }
    return 1;
}
void retro_magnetic_reset_smoothing(retro_magnetic *s) {
    if (s && s->started) for (unsigned i=0;i<3;++i) s->smooth[i].initial=true;
}
void retro_magnetic_reset(retro_magnetic *s) {
    if (!s) return;
    for (unsigned i=0;i<3;++i) s->smooth[i]=(smoother){.initial=true};
    memset(s->old,0,sizeof(s->old));
    memset(s->target,0,sizeof(s->target));
    memset(s->delta,0,sizeof(s->delta));
    /* Constructor's initial dropout interpolation endpoints. */
    s->old[3]=0; s->old[4]=0; s->old[5]=-.1f; s->old[6]=0;
    s->phase[0]=s->phase[1]=.25f;
    s->started=false; s->stereo_changed=true; s->remaining=0;
}
int retro_magnetic_prepare(retro_magnetic *s,float rate) {
    if (!s || !isfinite(rate) || rate<1000 || rate>1536000) return 0;
    s->inverse_rate=1.0f/rate;
    s->max_step=((rate*4.999999873689376e-05f)*.015625f)*16384.0f;
    s->feed=1.0f-expf((-3.1415927f*s->max_step)/.001f);
    retro_magnetic_reset(s); return 1;
}
static void controls(retro_magnetic *s) {
    s->remaining=64;
    const retro_magnetic_params *p=&s->p;
    s->skip=p->enable<.5f || (p->depth==0 && p->wear_depth==0);
    if (s->skip) { s->started=false; return; }
    s->started=true;
    float depth=p->depth - shaped(p->depth_steer)*p->depth;
    depth=smooth_value(s,0,depth);
    float wear=p->wear_depth*shaped(p->wear_gain);
    /* Native mono mode leaves its cached right wear gain untouched. */
    float gl=clamp(1.0f-wear,.031f,1),gr=s->target[2];
    if (p->wear_stereo==1) {
        gl=clamp(1.0f-(p->wear_pan<0?1.0f:1.0f-fminf(1,p->wear_pan))*wear,.031f,1);
        gr=clamp(1.0f-(p->wear_pan<-1?0.0f:fminf(0,p->wear_pan)+1.0f)*wear,.031f,1);
    }
    s->increment=s->inverse_rate*retro_scale_fit3(p->rate,6,30,20);
    if (s->stereo_changed) {
        s->phase[0]=0; s->phase[1]=p->stereo_phase*.5f; s->stereo_changed=false;
    }
    float hi=smooth_value(s,2,p->dropout_max);
    float lo=smooth_value(s,1,p->dropout_min);
    float *t=s->target;
    t[0]=depth; t[1]=clamp(gl,0,1); t[2]=clamp(gr,0,1);
    t[3]=clamp(p->dropout_steer,0,1); t[4]=hi;
    t[5]=lo; t[6]=1.0f/(hi-lo); t[7]=clamp(p->wear_pan*.5f,-1,1);
    for (unsigned i=0;i<8;++i) s->delta[i]=(s->old[i]-t[i])*.015625f;
    s->depth_gate=depth+s->delta[0];
    s->gain_boost=1.0f+0.412537544965744f*depth;
}
void retro_magnetic_process(retro_magnetic *s,float *left,float *right,size_t frames) {
    if (!s || !left || !right || s->inverse_rate==0) return;
    for (size_t i=0;i<frames;++i) {
        if (!s->remaining) controls(s);
        unsigned n=--s->remaining;
        if (s->skip) continue;
        float v[8];
        for (unsigned k=0;k<8;++k) v[k]=(float)n*s->delta[k]+s->target[k];
        v[5]=((float)n*.015625f)*(s->old[5]-s->target[5])+s->target[5];
        v[6]=((float)n*.015625f)*(s->old[6]-s->target[6])+s->target[6];
        float dropout=(clamp(v[3],v[5],v[4])-v[5])*v[6];
        float loss=1.0f-dropout;
        float dg[2];
        dg[0]=clamp(1.0f-(v[7]<-1?0.0f:fminf(0,v[7])+1.0f)*loss,0,1);
        dg[1]=clamp(1.0f-(v[7]<0?1.0f:1.0f-fminf(1,v[7]))*loss,0,1);
        for (unsigned c=0;c<2;++c) {
            float gain=1;
            if (s->depth_gate>0) {
                float wave=sine(s->phase[c]);
                s->phase[c]+=s->increment;
                while (s->phase[c]>=1) s->phase[c]+=-1.0f;
                gain=(((wave+-1.0f)*.5f)*v[0]+1.0f)*s->gain_boost;
            }
            if (s->p.wear_depth!=0) gain*=clamp(v[s->p.wear_stereo==1?1+c:1],0,1);
            if (s->p.dropout_amount!=0) gain*=s->p.wear_stereo==1?dg[c]:dropout;
            if (c==0) left[i]*=gain; else right[i]*=gain;
        }
        if (!s->remaining) memcpy(s->old,s->target,sizeof(s->old));
    }
}
