#ifndef RS_NOISE_TONE_H
#define RS_NOISE_TONE_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    float rate, requested, smoothed, applied;
    float max_step, smoothing;
    float coefficient[5], history[2][4];
    float correction, prior_correction, correction_step;
    unsigned remaining;
    bool snap, filtering;
} rs_noise_tone;

/* Preparation sets the target to neutral. Set the desired control after
   preparing. Changes are latched at the start of each 64-frame batch. */
bool rs_noise_tone_prepare(rs_noise_tone *tone, float rate);
bool rs_noise_tone_set(rs_noise_tone *tone, float normalized);
void rs_noise_tone_reset(rs_noise_tone *tone);
void rs_noise_tone_process(rs_noise_tone *tone, float *left, float *right, size_t frames);

#endif
