#ifndef RETRO_DRIVE_H
#define RETRO_DRIVE_H
#include <stddef.h>
typedef struct retro_drive retro_drive;
typedef struct { float amount, character, mix, enable; } retro_drive_params;
retro_drive_params retro_drive_defaults(void);
retro_drive *retro_drive_create(void);
void retro_drive_destroy(retro_drive *s);
int retro_drive_set(retro_drive *s,const retro_drive_params *p);
int retro_drive_prepare(retro_drive *s,float sample_rate);
void retro_drive_reset(retro_drive *s);
void retro_drive_process(retro_drive *s,float *left,float *right,size_t frames);
#endif
