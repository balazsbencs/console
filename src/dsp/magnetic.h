#ifndef RETRO_MAGNETIC_H
#define RETRO_MAGNETIC_H
#include <stddef.h>
typedef struct retro_magnetic retro_magnetic;
typedef struct {
    float depth, depth_steer, rate, stereo_phase;
    float wear_gain, wear_pan, wear_stereo, wear_depth;
    float dropout_steer, dropout_amount, dropout_min, dropout_max, enable;
} retro_magnetic_params;
retro_magnetic_params retro_magnetic_defaults(void);
retro_magnetic *retro_magnetic_create(void);
void retro_magnetic_destroy(retro_magnetic *s);
int retro_magnetic_set(retro_magnetic *s, const retro_magnetic_params *p);
int retro_magnetic_prepare(retro_magnetic *s, float sample_rate);
void retro_magnetic_reset(retro_magnetic *s);
void retro_magnetic_reset_smoothing(retro_magnetic *s);
void retro_magnetic_process(retro_magnetic *s, float *left, float *right, size_t frames);
#endif
