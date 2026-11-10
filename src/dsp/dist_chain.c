#include "dist_chain.h"
#include "magic.h"
#include "gain.h"
#include "relations.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
struct retro_dist_chain {
    retro_dist_chain_params p;
    rs_dist_flux flux;
    rs_dist_targets target;
    retro_magic *engine[3];
    rs_gain gain[6]; /* global pre/post, Tube pre/post, Iron pre/post */
    float rate;
    unsigned remaining, character, enabled;
    int32_t seed;
};
void rs_dist_flux_reset(rs_dist_flux *s,int32_t seed) {
    rs_noisegen_reset(&s->generator,seed);s->output=0;
}
void rs_dist_flux_tick(rs_dist_flux *s,float rate,float magnitude,float flux,float amount) {
    float group=rs_relation_mul_triple(magnitude,flux,amount,.5f,.5f,.99f);
    float gain=rs_relation_from_to_knob(group,0,.16f,.36f,.5f,.5f);
    float smooth=rs_relation_from_to_knob(group,.913f,.913f,.1f,.5f,.5f);
    rs_noisegen_set_params(&s->generator,gain,0,smooth,.405f,1,rate*.015625f);
    s->output=rs_noisegen_get_value(&s->generator)*16.0f;
}
void rs_dist_flux_targets(const rs_dist_flux *s,const retro_dist_chain_params *p,rs_dist_targets *t) {
    float a=p->magnitude*p->amount;
    float feed=rs_relation_modulate(.5f,s->output,.75f);
    t->scaled_amount=a;
    t->mix=p->mix*rs_relation_from_to(a,0,1,0,1,.99f);
    t->input_gain=rs_relation_knob_to_gain(feed,-3,20,.5f,.5f);
    t->output_gain=rs_relation_knob_to_gain(feed,3,-20,.5f,.5f);
    t->tube_db=22.0f*a;
    t->iron_db=rs_relation_from_to(a,0,1,0,18,.4f);
    t->engine_amount[0]=100.0f*a;
    t->engine_amount[1]=75.0f*a;
    t->engine_amount[2]=rs_relation_from_to(a,0,1,0,100,.4f);
}
retro_dist_chain_params retro_dist_chain_defaults(void) {
    return (retro_dist_chain_params){.amount=.5f,.mix=1,.magnitude=1,.low_hz=20,.high_hz=20000,.enable=1};
}
retro_dist_chain *retro_dist_chain_create(void) {
    retro_dist_chain *s=calloc(1,sizeof(*s));
    if(!s) return NULL;
    s->p=retro_dist_chain_defaults();
    for(unsigned i=0;i<3;++i) {
        s->engine[i]=retro_magic_create();
        if(!s->engine[i]) {retro_dist_chain_destroy(s);return NULL;}
    }
    return s;
}
void retro_dist_chain_destroy(retro_dist_chain *s) {
    if(!s) return;
    for(unsigned i=0;i<3;++i) retro_magic_destroy(s->engine[i]);
    free(s);
}
int retro_dist_chain_set(retro_dist_chain *s,const retro_dist_chain_params *p) {
    if(!s || !p) return 0;
    const float unit[]={p->amount,p->mix,p->magnitude,p->flux};
    for(unsigned i=0;i<4;++i) if(!isfinite(unit[i]) || unit[i]<0 || unit[i]>1) return 0;
    if(!isfinite(p->character) || p->character<0 || p->character>2 || p->character!=floorf(p->character) ||
       !isfinite(p->seed) || p->seed<0 || p->seed>65535 || p->seed!=floorf(p->seed) ||
       !isfinite(p->low_hz) || p->low_hz<20 || p->low_hz>20000 ||
       !isfinite(p->high_hz) || p->high_hz<20 || p->high_hz>20000 ||
       (p->enable!=0 && p->enable!=1)) return 0;
    s->p=*p;return 1;
}
static void targets(retro_dist_chain *s) {
    rs_dist_flux_targets(&s->flux,&s->p,&s->target);
    const rs_dist_targets *t=&s->target;
    (void)rs_gain_set(&s->gain[0],1,-12);(void)rs_gain_set(&s->gain[1],1,12);
    (void)rs_gain_set(&s->gain[2],1,t->tube_db);(void)rs_gain_set(&s->gain[3],1,-t->tube_db);
    (void)rs_gain_set(&s->gain[4],1,t->iron_db);(void)rs_gain_set(&s->gain[5],1,-t->iron_db);
    for(unsigned i=0;i<3;++i) {
        retro_magic_params p=retro_magic_defaults();
        p.character=(float)i;p.amount=t->engine_amount[i];p.mix=100;
        p.low_hz=s->p.low_hz;p.high_hz=s->p.high_hz;
        (void)retro_magic_set(s->engine[i],&p);
    }
    s->character=(unsigned)s->p.character;s->enabled=(unsigned)s->p.enable;
}
void retro_dist_chain_reset(retro_dist_chain *s) {
    if(!s) return;
    s->seed=(int32_t)s->p.seed;rs_dist_flux_reset(&s->flux,s->seed);targets(s);
    for(unsigned i=0;i<6;++i) rs_gain_reset(&s->gain[i]);
    for(unsigned i=0;i<3;++i) retro_magic_reset(s->engine[i]);
    s->remaining=0;
}
int retro_dist_chain_prepare(retro_dist_chain *s,float rate) {
    if(!s || !isfinite(rate) || rate<1000 || rate>1536000) return 0;
    targets(s);
    for(unsigned i=0;i<6;++i) if(!rs_gain_prepare(&s->gain[i],rate)) return 0;
    for(unsigned i=0;i<3;++i) if(!retro_magic_prepare(s->engine[i],rate)) return 0;
    s->rate=rate;retro_dist_chain_reset(s);return 1;
}
static void tick(retro_dist_chain *s) {
    if(s->seed!=(int32_t)s->p.seed) {s->seed=(int32_t)s->p.seed;rs_dist_flux_reset(&s->flux,s->seed);}
    rs_dist_flux_tick(&s->flux,s->rate,s->p.magnitude,s->p.flux,s->p.amount);
    targets(s);s->remaining=64;
}
void retro_dist_chain_process(retro_dist_chain *s,float *left,float *right,size_t frames) {
    if(!s || !left || !right || !s->rate) return;
    for(size_t i=0;i<frames;++i) {
        if(!s->remaining) tick(s);
        --s->remaining;
        if(!s->enabled) continue;
        float original[2]={left[i],right[i]};
        float input_gain=s->target.input_gain*s->target.mix;
        float l=original[0]*input_gain,r=original[1]*input_gain;
        rs_gain_process(&s->gain[0],&l,&r,1);
        if(s->character==0) rs_gain_process(&s->gain[2],&l,&r,1);
        else if(s->character==2) rs_gain_process(&s->gain[4],&l,&r,1);
        retro_magic_process(s->engine[s->character],&l,&r,1);
        if(s->character==0) rs_gain_process(&s->gain[3],&l,&r,1);
        else if(s->character==2) rs_gain_process(&s->gain[5],&l,&r,1);
        rs_gain_process(&s->gain[1],&l,&r,1);
        float dry=1.0f-s->target.mix;
        left[i]=original[0]*dry+l*s->target.output_gain;
        right[i]=original[1]*dry+r*s->target.output_gain;
    }
}
