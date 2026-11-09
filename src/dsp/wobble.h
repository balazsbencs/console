#ifndef RETRO_WOBBLE_H
#define RETRO_WOBBLE_H
#include <stddef.h>

typedef struct retro_wobble retro_wobble;
/* Original normalized metadata inputs. stereo_phase is a phase offset [0,.4];
   lfo_noise is the external bipolar [-1,1] perturbation input. The original
   macro/Flux graph lives outside this kernel. Calls on one instance serialize. */
typedef struct {
    float wow_depth, flutter_depth, wow_rate, flutter_rate;
    float stereo_phase, mix, lfo_noise, noise_scale, enable;
} retro_wobble_params;

retro_wobble_params retro_wobble_defaults(void);
retro_wobble *retro_wobble_create(void);
void retro_wobble_destroy(retro_wobble *dsp);
int retro_wobble_set(retro_wobble *dsp, const retro_wobble_params *params);
/* Allocation is confined to create/prepare/destroy, never audio processing. */
int retro_wobble_prepare(retro_wobble *dsp, float sample_rate);
/* Study full reset clears delay history as well as native phase/smoothing. */
void retro_wobble_reset(retro_wobble *dsp);
void retro_wobble_reset_phase(retro_wobble *dsp);
void retro_wobble_reset_smoothing(retro_wobble *dsp);
void retro_wobble_process(retro_wobble *dsp, const float *left, const float *right,
                          float *out_left, float *out_right, size_t frames);
#endif
