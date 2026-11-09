#ifndef RETRO_WOBBLE_MACRO_H
#define RETRO_WOBBLE_MACRO_H
#include "noisegen.h"
#include "wobble.h"

/* Recovered settled Wobble macro graph. Controls are finite [0,1] and
   sample_rate is positive. Tick once per 64 audio frames. Original production
   relation-table seed indices remain unknown; callers supply explicit seeds. */
typedef struct {
    rs_noisegen wow, flutter;
    float wow_output, flutter_output;
} rs_wobble_macro;

void rs_wobble_macro_reset(rs_wobble_macro *s, int32_t wow_seed, int32_t flutter_seed);
void rs_wobble_macro_tick(rs_wobble_macro *s, float sample_rate,
                          float magnitude, float flux, float amount);
/* Preserves rate, mix and enable fields; sets normalized depths, stereo phase,
   and the two external wow noise inputs on an existing kernel parameter set. */
void rs_wobble_macro_apply(const rs_wobble_macro *s, float magnitude, float flux,
                           float amount, float balance, float stereo,
                           retro_wobble_params *params);
#endif
