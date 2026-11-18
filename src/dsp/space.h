#ifndef RETRO_SPACE_H
#define RETRO_SPACE_H
#include <stddef.h>
typedef struct retro_space retro_space;
/* Physical filter Hz and predelay ms; other continuous controls are normalized. */
typedef struct {
    float enable, decay, dampen_hz, soften_hz, level, stereo_detune;
    float stereo_mode, dual_mono, feedbacks, allpass, reverb_mode;
    float pre_delay_ms, pre_wobble_noise, pre_wobble_amount;
} retro_space_params;
retro_space_params retro_space_defaults(void);
retro_space *retro_space_create(void);
void retro_space_destroy(retro_space *s);
int retro_space_set(retro_space *s,const retro_space_params *p);
int retro_space_prepare(retro_space *s,float rate);
void retro_space_reset(retro_space *s);
void retro_space_reset_smoothing(retro_space *s);
/* Stereo in-place processing; controls latch on a64frame study cadence. */
void retro_space_process(retro_space *s,float *left,float *right,size_t frames);
#endif
