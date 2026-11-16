#ifndef RETRO_CLASSIC_CHAIN_H
#define RETRO_CLASSIC_CHAIN_H
#include "dist_chain.h"
typedef struct retro_classic_chain retro_classic_chain;
typedef retro_dist_chain_params retro_classic_chain_params;
typedef struct {
    float scaled_amount, mix, input_gain, output_gain, pre_db, post_db;
    float eq_db[3], engine_amount[3], engine_db[3], output_hz[3], reference_db[3];
} rs_classic_targets;
void rs_classic_chain_targets(const rs_dist_flux *flux,const retro_classic_chain_params *p,rs_classic_targets *out);
retro_classic_chain_params retro_classic_chain_defaults(void);
retro_classic_chain *retro_classic_chain_create(void);
void retro_classic_chain_destroy(retro_classic_chain *s);
int retro_classic_chain_set(retro_classic_chain *s,const retro_classic_chain_params *p);
int retro_classic_chain_prepare(retro_classic_chain *s,float rate);
void retro_classic_chain_reset(retro_classic_chain *s);
/* Settled routing and controls latch every64frames; disabled audio freezes,
   while Flux continues. Separate histories for all three classic characters. */
void retro_classic_chain_process(retro_classic_chain *s,float *left,float *right,size_t frames);
#endif
