#ifndef RETRO_NOISE_COMP_H
#define RETRO_NOISE_COMP_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    float enable, threshold, ratio, attack, release, averaging, gain_comp, highpass;
} rs_noise_comp_params;
typedef struct {
    union {
        float f[24];
        uint32_t u[24];
    } native;
    rs_noise_comp_params p, active;
    float rate, threshold_db, ratio_coefficient, gain_offset;
    unsigned remaining;
} rs_noise_comp;
rs_noise_comp_params rs_noise_comp_defaults(void);
int rs_noise_comp_set(rs_noise_comp *s, const rs_noise_comp_params *p);
int rs_noise_comp_prepare(rs_noise_comp *s, float rate);
void rs_noise_comp_reset(rs_noise_comp *s);
/* Stereo linked compression. NULL sidechain channels supply zero samples.
   Controls latch every64frames; active input marker is fixed to1. */
void rs_noise_comp_process(rs_noise_comp *s, float *left, float *right, const float *side_left,
                           const float *side_right, size_t frames);
#endif
