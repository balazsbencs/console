#ifndef RETRO_MAGIC_H
#define RETRO_MAGIC_H
#include <stddef.h>
typedef struct retro_magic retro_magic;
/* Physical engine inputs; character is 0 Tube, 1 Air, 2 Iron. */
typedef struct {
    float amount, character, low_hz, high_hz, dist_db, pass_db, mix, output_hz;
} retro_magic_params;
retro_magic_params retro_magic_defaults(void);
retro_magic *retro_magic_create(void);
void retro_magic_destroy(retro_magic *s);
int retro_magic_set(retro_magic *s,const retro_magic_params *p);
int retro_magic_prepare(retro_magic *s,float rate);
void retro_magic_reset(retro_magic *s);
/* Stereo, distinct channel buffers; preserves native64frame Process cleanup. */
void retro_magic_process(retro_magic *s,float *left,float *right,size_t frames);
#endif
