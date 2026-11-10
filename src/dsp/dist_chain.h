#ifndef RETRO_DIST_CHAIN_H
#define RETRO_DIST_CHAIN_H
#include <stddef.h>
#include <stdint.h>
#include "noisegen.h"
typedef struct retro_dist_chain retro_dist_chain;
typedef struct {
    float amount, character, mix, magnitude, flux, seed, low_hz, high_hz, enable;
} retro_dist_chain_params;
typedef struct { rs_noisegen generator; float output; } rs_dist_flux;
typedef struct {
    float scaled_amount, mix, input_gain, output_gain, tube_db, iron_db;
    float engine_amount[3];
} rs_dist_targets;
void rs_dist_flux_reset(rs_dist_flux *s,int32_t seed);
void rs_dist_flux_tick(rs_dist_flux *s,float rate,float magnitude,float flux,float amount);
void rs_dist_flux_targets(const rs_dist_flux *s,const retro_dist_chain_params *p,rs_dist_targets *out);
retro_dist_chain_params retro_dist_chain_defaults(void);
retro_dist_chain *retro_dist_chain_create(void);
void retro_dist_chain_destroy(retro_dist_chain *s);
int retro_dist_chain_set(retro_dist_chain *s,const retro_dist_chain_params *p);
int retro_dist_chain_prepare(retro_dist_chain *s,float rate);
void retro_dist_chain_reset(retro_dist_chain *s);
/* Settled controls latch per64frames. Enable freezes audio but Flux advances. */
void retro_dist_chain_process(retro_dist_chain *s,float *left,float *right,size_t frames);
#endif
