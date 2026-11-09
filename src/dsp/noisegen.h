#ifndef RETRO_STUDY_NOISEGEN_H
#define RETRO_STUDY_NOISEGEN_H
#include <stdbool.h>
#include <stdint.h>
/* Recovered cNoiseGenData layout, 40 bytes. Reset preserves parameters. */
typedef struct {
    float envelope, smooth1, smooth2;
    uint32_t rng_x, rng_y;
    uint8_t bipolar, reserved[3];
    float decay_coefficient, smooth_coefficient, shape, gain;
} rs_noisegen;
void rs_noisegen_reset(rs_noisegen *state, int32_t seed);
void rs_noisegen_set_params(rs_noisegen *state, float gain, float chaos,
                            float smooth, float decay, bool bipolar,
                            float sample_rate);
float rs_noisegen_get_value(rs_noisegen *state);
#endif
