#include "noise_follow.h"
#include "noise_biquad.h"
#include "noise_follow_filter.h"

#include <math.h>
#include <string.h>

enum { FOLLOW_BATCH = 64 };

/* Settled physical values from the constructor's Scale_Fit3 metadata. */
static const float cutoff = 79.99999237060547f;
static const float attack_seconds = .009999999776482582f;
static const float decay_zero = 2.9970028400421143f;
static const float decay_one = .015235929749906063f;
static const float gain_compensation = 3.000000238418579f;

void rs_noise_follow_reset(rs_noise_follow *follow) {
    if (!follow)
        return;
    memset(follow->history, 0, sizeof(follow->history));
    follow->envelope = 0.0f;
    follow->peak = 0.0f;
    follow->remaining = 0;
}

bool rs_noise_follow_prepare(rs_noise_follow *follow, float rate) {
    if (!follow || !isfinite(rate) || rate < 1000.0f || rate > 1536000.0f)
        return false;
    *follow = (rs_noise_follow){.rate = rate, .playing = true};
    follow->attack = 1.0f - expf(-2.0f / (attack_seconds * rate));
    rs_noise_follow_filter(rate, cutoff, follow->coefficient);
    return true;
}

bool rs_noise_follow_set(rs_noise_follow *follow, float normalized) {
    if (!follow || !isfinite(normalized) || normalized < 0 || normalized > 1)
        return false;
    follow->requested = normalized;
    return true;
}

void rs_noise_follow_set_playing(rs_noise_follow *follow, bool playing) {
    if (follow)
        follow->playing = playing;
}

static void begin_batch(rs_noise_follow *follow) {
    follow->remaining = FOLLOW_BATCH;
    float amount = follow->requested;
    follow->bypass = amount == 0 && follow->playing;
    if (follow->bypass) {
        follow->envelope = follow->peak = 1.0001000165939331f;
        return;
    }

    float decay = 1.0f;
    if (amount != 0) {
        const float curve = .94f;
        float weight = (amount * curve) / (((curve + curve + -1.0f) * amount + 1.0f) - curve);
        decay = weight * (decay_one - decay_zero) + decay_zero;
    }
    follow->release = 1.0f - expf(-2.0f / (decay * follow->rate));
    float weight = fminf(amount + amount, 1.0f);
    weight *= weight;
    float threshold = (.5f + -.0003162f) * weight + .0003162f;
    follow->ceiling = threshold * 1.05f;

    float inverse_threshold = 1.0f / threshold;
    weight *= weight;
    follow->maximum = weight * (gain_compensation + -1.0f) + 1.0f;
    follow->detector_scale = inverse_threshold * follow->maximum;
    float biased_floor = follow->detector_scale * .0001f;
    follow->recenter = 1.0f / (1.0f - biased_floor);
    follow->offset = biased_floor + .0001f;
}

static float follow_sample(rs_noise_follow *follow, float sample) {
    float filtered = rs_noise_biquad_sample(follow->coefficient, follow->history, sample);
    float detector = fminf(fabsf(filtered), follow->ceiling) * follow->detector_scale + .0001f;
    bool falling = detector < follow->envelope;
    float target = falling ? detector * 1.4f : detector;
    float coefficient = falling ? follow->release : follow->attack;
    follow->envelope = (target - follow->envelope) * coefficient + follow->envelope;
    float peak = follow->envelope;
    if (peak < follow->peak)
        peak = (peak * 1.4f - follow->peak) * follow->release + follow->peak;
    follow->peak = peak;
    float gain = (peak - follow->offset) * follow->recenter;
    return gain < 0 ? 0 : fminf(gain, follow->maximum);
}

void rs_noise_follow_process(rs_noise_follow *follow, const float *dry_left,
                             const float *dry_right, float *gains, size_t frames) {
    if (!follow || !follow->rate || !dry_left || !dry_right || !gains)
        return;
    while (frames) {
        if (!follow->remaining)
            begin_batch(follow);
        size_t count = follow->remaining < frames ? follow->remaining : frames;
        if (follow->bypass) {
            for (size_t i = 0; i < count; ++i)
                gains[i] = 1.0f;
        } else {
            for (size_t i = 0; i < count; ++i)
                gains[i] = follow_sample(follow, (dry_left[i] + dry_right[i]) * .5f);
        }
        follow->remaining -= (unsigned)count;
        dry_left += count;
        dry_right += count;
        gains += count;
        frames -= count;
    }
}
