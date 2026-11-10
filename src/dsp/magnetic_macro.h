#ifndef RETRO_MAGNETIC_MACRO_H
#define RETRO_MAGNETIC_MACRO_H
#include "magnetic.h"
#include "noisegen.h"
/* Settled resource graph; controls are finite [0,1]. Explicit seeds replace
   unresolved production relation-table indices. Tick once per 64 frames. */
typedef struct {
    rs_noisegen depth, dropout, wear, pan;
    float depth_output, dropout_output, wear_output, pan_output;
} rs_magnetic_macro;
void rs_magnetic_macro_reset(rs_magnetic_macro *s, int32_t depth_seed,
    int32_t dropout_seed, int32_t wear_seed, int32_t pan_seed);
void rs_magnetic_macro_tick(rs_magnetic_macro *s, float sample_rate,
    float magnitude, float flux, float amount, float dropouts);
/* Preserves rate, stereo phase, wear stereo and enable. */
void rs_magnetic_macro_apply(const rs_magnetic_macro *s, float magnitude,
    float amount, float balance, float dropouts, retro_magnetic_params *params);
#endif
