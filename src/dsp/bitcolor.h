#ifndef RETRO_BITCOLOR_H
#define RETRO_BITCOLOR_H
#include <stddef.h>

/* Recovered cBitColor stereo kernel. Flux generation lives outside this block.
 * normalized=1 interprets bits/rate as the original decreasing control curves.
 * No allocation, locks or plugin-host dependencies in this DSP API. */
typedef struct {
    float bits, rate, smooth, mix, feedback;
    float low, high, round_error, stereo, aa2, aa3;
    float focus, mod_rate, mod_depth, normalized;
} retro_digital_params;

typedef struct {
    float hp_input, hp_output, lp_output;
    float hp2_input, hp2_output, lp2_output;
    float phase, ring[32], slew, filtered;
    unsigned position;
} retro_digital_channel;

typedef struct {
    retro_digital_params params;
    float sample_rate, bits_state, mix_state;
    float bits_pole, bits_feed, mix_pole, mix_feed;
    float hp_pole, hp_feed, lp_pole, lp_feed;
    float advance, step, levels, quant_gain, rounding, blend_scale;
    float slew_limit, output_feed, feedback_gain, wet_target;
    float rate_state, gain_state, gain_target, mod_phase;
    unsigned control_remaining;
    retro_digital_channel channels[2];
} retro_digital;

retro_digital_params retro_digital_defaults(void);
int retro_digital_prepare(retro_digital *dsp, float sample_rate);
int retro_digital_set(retro_digital *dsp, const retro_digital_params *params);
void retro_digital_reset(retro_digital *dsp);
void retro_digital_process(retro_digital *dsp, const float *left, const float *right,
                           float *out_left, float *out_right, size_t frames);
#endif
