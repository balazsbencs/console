#include "classic_chain.h"
#include "classic.h"
#include "peak_eq.h"
#include "gain.h"
#include "relations.h"
#include <math.h>
#include <stdlib.h>
struct retro_classic_chain {
    retro_classic_chain_params p;
    rs_dist_flux flux;
    rs_classic_targets target;
    retro_classic *engine[3];
    rs_peak_eq eq[3][2];
    rs_gain gain[4]; /* global pre/post, shared classic pre/post */
    float rate;
    unsigned remaining, character, enabled;
    int32_t seed;
};
static float map(float a,float lo,float hi,float curve) {
    if(curve==.5f) return (hi-lo)*a+lo;
    return rs_relation_from_to(a,0,1,lo,hi,curve);
}
void rs_classic_chain_targets(const rs_dist_flux *flux,const retro_classic_chain_params *p,rs_classic_targets *t) {
    rs_dist_targets shared;rs_dist_flux_targets(flux,p,&shared);
    t->scaled_amount=shared.scaled_amount;t->mix=shared.mix;
    t->input_gain=shared.input_gain;t->output_gain=shared.output_gain;
    float a=t->scaled_amount;
    t->pre_db=map(a,-19,0,.5f);t->post_db=map(a,19,0,.5f);
    t->eq_db[0]=map(a,10,12,.5f);t->eq_db[1]=map(a,10,12,.15f);t->eq_db[2]=map(a,10,16,.5f);
    t->engine_amount[0]=map(a,0,100,.5f);t->engine_amount[1]=map(a,0,100,.15f);t->engine_amount[2]=map(a,0,100,.5f);
    t->engine_db[0]=map(a,0,-3,.5f);t->engine_db[1]=map(a,0,-3,.15f);t->engine_db[2]=map(a,0,-1,.5f);
    t->output_hz[0]=map(a,20000,5000,.5f);t->output_hz[1]=map(a,20000,7000,.15f);t->output_hz[2]=map(a,10000,3000,.5f);
    t->reference_db[0]=-10;t->reference_db[1]=-15;t->reference_db[2]=map(a,0,-6,.5f);
}
retro_classic_chain_params retro_classic_chain_defaults(void) {return retro_dist_chain_defaults();}
retro_classic_chain *retro_classic_chain_create(void) {
    retro_classic_chain *s=calloc(1,sizeof(*s));if(!s) return NULL;
    s->p=retro_classic_chain_defaults();
    for(unsigned i=0;i<3;++i) {s->engine[i]=retro_classic_create();if(!s->engine[i]) {retro_classic_chain_destroy(s);return NULL;}}
    return s;
}
void retro_classic_chain_destroy(retro_classic_chain *s) {
    if(!s) return;
    for(unsigned i=0;i<3;++i) retro_classic_destroy(s->engine[i]);
    free(s);
}
int retro_classic_chain_set(retro_classic_chain *s,const retro_classic_chain_params *p) {
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
static void targets(retro_classic_chain *s) {
    rs_classic_chain_targets(&s->flux,&s->p,&s->target);
    const rs_classic_targets *t=&s->target;
    (void)rs_gain_set(&s->gain[0],1,-12);(void)rs_gain_set(&s->gain[1],1,12);
    (void)rs_gain_set(&s->gain[2],1,t->pre_db);(void)rs_gain_set(&s->gain[3],1,t->post_db);
    for(unsigned i=0;i<3;++i) {
        retro_classic_params p=retro_classic_defaults();
        p.character=(float)i;p.amount=t->engine_amount[i];p.mix=100;
        p.low_hz=s->p.low_hz;p.high_hz=s->p.high_hz;p.dist_db=t->engine_db[i];
        p.output_hz=t->output_hz[i];p.reference_db=t->reference_db[i];
        (void)retro_classic_set(s->engine[i],&p);
        float hz=i==2?850:900,q=i==2?.2f:.4f;
        (void)rs_peak_eq_set(&s->eq[i][0],hz,q,t->eq_db[i]);
        (void)rs_peak_eq_set(&s->eq[i][1],hz,q,-t->eq_db[i]);
    }
    s->character=(unsigned)s->p.character;s->enabled=(unsigned)s->p.enable;
}
void retro_classic_chain_reset(retro_classic_chain *s) {
    if(!s) return;
    s->seed=(int32_t)s->p.seed;rs_dist_flux_reset(&s->flux,s->seed);targets(s);
    for(unsigned i=0;i<4;++i) rs_gain_reset(&s->gain[i]);
    for(unsigned i=0;i<3;++i) {
        retro_classic_reset(s->engine[i]);
        for(unsigned j=0;j<2;++j) rs_peak_eq_reset(&s->eq[i][j]);
    }
    s->remaining=0;
}
int retro_classic_chain_prepare(retro_classic_chain *s,float rate) {
    if(!s || !isfinite(rate) || rate<1000 || rate>1536000) return 0;
    targets(s);
    for(unsigned i=0;i<4;++i) if(!rs_gain_prepare(&s->gain[i],rate)) return 0;
    for(unsigned i=0;i<3;++i) {
        if(!retro_classic_prepare(s->engine[i],rate)) return 0;
        for(unsigned j=0;j<2;++j) if(!rs_peak_eq_prepare(&s->eq[i][j],rate)) return 0;
    }
    s->rate=rate;retro_classic_chain_reset(s);return 1;
}
static void tick(retro_classic_chain *s) {
    if(s->seed!=(int32_t)s->p.seed) {s->seed=(int32_t)s->p.seed;rs_dist_flux_reset(&s->flux,s->seed);}
    rs_dist_flux_tick(&s->flux,s->rate,s->p.magnitude,s->p.flux,s->p.amount);
    targets(s);s->remaining=64;
}
void retro_classic_chain_process(retro_classic_chain *s,float *left,float *right,size_t frames) {
    if(!s || !left || !right || !s->rate) return;
    for(size_t i=0;i<frames;++i) {
        if(!s->remaining) tick(s);
        --s->remaining;
        if(!s->enabled) continue;
        float original[2]={left[i],right[i]};
        float input_gain=s->target.input_gain*s->target.mix;
        float l=original[0]*input_gain,r=original[1]*input_gain;
        rs_gain_process(&s->gain[0],&l,&r,1);rs_gain_process(&s->gain[2],&l,&r,1);
        rs_peak_eq_process(&s->eq[s->character][0],&l,&r,1);
        retro_classic_process(s->engine[s->character],&l,&r,1);
        rs_peak_eq_process(&s->eq[s->character][1],&l,&r,1);
        rs_gain_process(&s->gain[3],&l,&r,1);rs_gain_process(&s->gain[1],&l,&r,1);
        float dry=1.0f-s->target.mix;
        left[i]=original[0]*dry+l*s->target.output_gain;
        right[i]=original[1]*dry+r*s->target.output_gain;
    }
}
