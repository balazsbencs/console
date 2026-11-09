#ifndef RETRO_DIGITAL_FLUX_H
#define RETRO_DIGITAL_FLUX_H
#include "noisegen.h"

/* Caller owns the 64-frame clock. Controls are finite normalized [0,1],
   sample_rate is positive. Seeds are serialized relation indices in the
   original; the study accepts explicit seeds because their actual indices
   have not been captured from the original model. */
typedef struct {
    rs_noisegen bits, rate;
    float bit_output, rate_output;
} rs_digital_flux;

void rs_digital_flux_reset(rs_digital_flux *s, int32_t bit_seed, int32_t rate_seed);
void rs_digital_flux_tick(rs_digital_flux *s, float sample_rate,
                          float magnitude, float flux, float amount);
void rs_digital_flux_targets(const rs_digital_flux *s, float magnitude,
                             float amount, float balance, float *bits, float *rate);
#endif
