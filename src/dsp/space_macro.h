#ifndef RETRO_SPACE_MACRO_H
#define RETRO_SPACE_MACRO_H
#include "noisegen.h"
#include "space.h"
/* Settled resource graph. Explicit seed replaces unresolved production index.
   Tick once per64frames even when Space Enable, Amount or Flux is zero. */
typedef struct { rs_noisegen generator; float output; } rs_space_macro;
void rs_space_macro_reset(rs_space_macro *s, int32_t seed);
void rs_space_macro_tick(rs_space_macro *s, float sample_rate);
/* Writes only Level, PreWobbleNoiseAmount and PreWobbleNoise.
   M/A/F are finite normalized controls; preserves the other kernel controls. */
void rs_space_macro_apply(const rs_space_macro *s, float magnitude, float amount,
                          float flux, retro_space_params *params);
#endif
