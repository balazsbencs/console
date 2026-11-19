#ifndef RS_NOISE_INTERP_H
#define RS_NOISE_INTERP_H
/* Four consecutive PCM frames around a fractional position between center
   and next. The caller supplies boundary samples from its source. */
float rs_noise_interpolate(float previous, float center, float next,
                           float following, float fraction);
#endif
