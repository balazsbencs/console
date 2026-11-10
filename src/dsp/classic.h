#ifndef RETRO_CLASSIC_H
#define RETRO_CLASSIC_H
#include <stddef.h>
typedef struct retro_classic retro_classic;
/* Physical processor controls: 0 Crunch, 1 Heavy, 2 Zap. */
typedef struct {
    float amount, character, low_hz, high_hz, dist_db, pass_db, mix, output_hz, reference_db;
} retro_classic_params;
retro_classic_params retro_classic_defaults(void);
retro_classic *retro_classic_create(void);
void retro_classic_destroy(retro_classic *s);
int retro_classic_set(retro_classic *s,const retro_classic_params *p);
int retro_classic_prepare(retro_classic *s,float rate);
void retro_classic_reset(retro_classic *s);
/* Distinct stereo buffers; native SIMD arithmetic and 64-frame cleanup cadence. */
void retro_classic_process(retro_classic *s,float *left,float *right,size_t frames);
#endif
