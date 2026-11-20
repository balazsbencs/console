#include "noise_player.h"
#include "noise_interp.h"

#include <math.h>
#include <string.h>

bool rs_noise_player_prepare(rs_noise_player *player, const float *left, const float *right,
                             uint32_t frames, double source_rate, double output_rate) {
    if (!player || !left || !right || !frames || !isfinite(source_rate) || source_rate <= 0 ||
        !isfinite(output_rate) || output_rate <= 0)
        return false;

    /* Match the native supplier's two divisions. Reduce whole-loop skips
       up front so every audio-thread wrap takes at most one subtraction. */
    double step = 1.0 / (output_rate / source_rate);
    if (!isfinite(step) || step <= 0)
        return false;
    *player = (rs_noise_player){
        .left = left, .right = right, .frames = frames,
        .step = source_rate == output_rate ? 1.0 : fmod(step, frames),
    };
    return true;
}

void rs_noise_player_reset(rs_noise_player *player) {
    if (player)
        player->phase = 0;
}

static uint32_t next_frame(uint32_t frame, uint32_t size) {
    return frame + 1 == size ? 0 : frame + 1;
}

void rs_noise_player_process(rs_noise_player *player, float *left, float *right, size_t frames) {
    if (!player || !player->frames || !left || !right || !frames)
        return;

    uint32_t size = player->frames;
    double phase = player->phase;
    if (player->step == 1.0) {
        /* Integer one-frame steps preserve the source floats exactly. */
        uint32_t center = (uint32_t)phase;
        while (frames) {
            size_t count = size - center;
            if (frames < count)
                count = frames;
            memcpy(left, player->left + center, count * sizeof(*left));
            memcpy(right, player->right + center, count * sizeof(*right));
            left += count;
            right += count;
            frames -= count;
            center += (uint32_t)count;
            if (center == size)
                center = 0;
        }
        player->phase = center;
        return;
    }

    for (size_t i = 0; i < frames; ++i) {
        uint32_t center = (uint32_t)phase;
        uint32_t previous = center ? center - 1 : size - 1;
        uint32_t next = next_frame(center, size);
        uint32_t following = next_frame(next, size);
        float fraction = (float)(phase - center);
        left[i] = rs_noise_interpolate(player->left[previous], player->left[center],
                                       player->left[next], player->left[following], fraction);
        right[i] = rs_noise_interpolate(player->right[previous], player->right[center],
                                        player->right[next], player->right[following], fraction);
        phase += player->step;
        if (phase >= size)
            phase -= size;
    }
    player->phase = phase;
}
