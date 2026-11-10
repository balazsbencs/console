#ifndef RS_GAIN_H
#define RS_GAIN_H
#include <stddef.h>
/* Fixed 64-frame original Process cadence with four-frame interpolation. */
typedef struct {
    float linear_gain, db, current, target, coefficient, block_coefficient;
    float block_target, group_start, group_delta;
    unsigned remaining, lane, smoothing;
} rs_gain;
int rs_gain_prepare(rs_gain *s, float sample_rate);
int rs_gain_set(rs_gain *s, float linear_gain, float db);
void rs_gain_reset(rs_gain *s);
void rs_gain_process(rs_gain *s, float *left, float *right, size_t frames);
#endif
