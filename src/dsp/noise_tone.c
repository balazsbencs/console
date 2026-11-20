#include "noise_tone.h"
#include "noise_tone_coeff.h"

#include <math.h>
#include <string.h>

enum { TONE_BATCH = 64 };

void rs_noise_tone_reset(rs_noise_tone *tone) {
    if (!tone)
        return;
    memset(tone->history, 0, sizeof(tone->history));
    tone->applied = -1.0f;
    tone->prior_correction = 1.0f;
    tone->remaining = 0;
    tone->snap = true;
    tone->filtering = false;
}

bool rs_noise_tone_prepare(rs_noise_tone *tone, float rate) {
    if (!tone || !isfinite(rate) || rate < 1000.0f || rate > 1536000.0f)
        return false;
    *tone = (rs_noise_tone){.rate = rate, .requested = .5f};
    tone->max_step = ((rate * .00002f) * .015625f) * 16384.0f;
    tone->smoothing = 1.0f - expf((tone->max_step * -3.1415927410125732f) / .001f);
    rs_noise_tone_reset(tone);
    return true;
}

bool rs_noise_tone_set(rs_noise_tone *tone, float normalized) {
    if (!tone || !isfinite(normalized) || normalized < 0 || normalized > 1)
        return false;
    tone->requested = normalized;
    return true;
}

static void begin_batch(rs_noise_tone *tone) {
    float target = tone->requested * 16384.0f;
    if (!tone->snap && target != tone->smoothed) {
        float step = (target - tone->smoothed) * tone->smoothing;
        if (fabsf(step) >= .016384f)
            target = tone->smoothed + copysignf(fminf(fabsf(step), tone->max_step), step);
    }
    tone->snap = false;
    tone->smoothed = target;
    float normalized = target * (1.0f / 16384.0f);
    if (normalized != tone->applied) {
        float db = (normalized + -.5f) * 48.0f;
        tone->applied = normalized;
        tone->filtering = db != 0;
        rs_noise_tone_coeff(tone->rate, db, tone->coefficient, &tone->correction);
        if (!tone->filtering)
            memset(tone->history, 0, sizeof(tone->history));
    }
    tone->correction_step = (tone->prior_correction - tone->correction) * (1.0f / TONE_BATCH);
    tone->remaining = TONE_BATCH;
}

static float filter_sample(const float c[5], float history[4], float input) {
    float sum = c[0] * input + c[1] * history[0];
    sum += c[2] * history[1];
    sum -= c[3] * history[2];
    sum -= c[4] * history[3];
    volatile float biased = sum + .0001f;
    float output = biased + -.0001f;
    history[1] = history[0];
    history[0] = input;
    history[3] = history[2];
    history[2] = output;
    return output;
}

void rs_noise_tone_process(rs_noise_tone *tone, float *left, float *right, size_t frames) {
    if (!tone || !tone->rate || !left || !right)
        return;
    while (frames) {
        if (!tone->remaining)
            begin_batch(tone);
        size_t count = tone->remaining < frames ? tone->remaining : frames;
        if (tone->filtering) {
            for (size_t i = 0; i < count; ++i) {
                float correction = (float)(tone->remaining - i - 1) * tone->correction_step + tone->correction;
                left[i] = filter_sample(tone->coefficient, tone->history[0], left[i]) * correction;
                right[i] = filter_sample(tone->coefficient, tone->history[1], right[i]) * correction;
            }
        }
        tone->remaining -= (unsigned)count;
        if (!tone->remaining && tone->filtering)
            tone->prior_correction = tone->correction;
        left += count;
        right += count;
        frames -= count;
    }
}
