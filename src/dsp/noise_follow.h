#ifndef RS_NOISE_FOLLOW_H
#define RS_NOISE_FOLLOW_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    float rate, requested;
    float coefficient[5], history[4], envelope, peak;
    float attack, release, ceiling, detector_scale, offset, recenter, maximum;
    unsigned remaining;
    bool playing, bypass;
} rs_noise_follow;

/* Prepares the original settled Noise defaults. The Follow control
   starts at 0 and the transport defaults to playing. */
bool rs_noise_follow_prepare(rs_noise_follow *follow, float rate);
bool rs_noise_follow_set(rs_noise_follow *follow, float normalized);
void rs_noise_follow_set_playing(rs_noise_follow *follow, bool playing);
void rs_noise_follow_reset(rs_noise_follow *follow);

/* Writes one envelope gain per frame. Parameter and transport changes
   apply at the next 64-frame boundary, independent of caller partitions. */
void rs_noise_follow_process(rs_noise_follow *follow, const float *dry_left,
                             const float *dry_right, float *gains, size_t frames);

#endif
