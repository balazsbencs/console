#ifndef RS_NOISE_PLAYER_H
#define RS_NOISE_PLAYER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const float *left, *right;
    uint32_t frames;
    double phase, step;
} rs_noise_player;

/* The caller retains the immutable planar PCM for the life of the player.
   Preparation starts at frame zero and loops over the whole logical clip. */
bool rs_noise_player_prepare(rs_noise_player *player, const float *left, const float *right,
                             uint32_t frames, double source_rate, double output_rate);
void rs_noise_player_reset(rs_noise_player *player);
/* Output buffers must be distinct and must not overlap source PCM. */
void rs_noise_player_process(rs_noise_player *player, float *left, float *right, size_t frames);

#endif
