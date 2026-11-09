#include "wobble.h"
#include "curves.h"
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float value; bool initial; } smoother;
struct retro_wobble {
    retro_wobble_params p;
    float sample_rate, inverse_rate, pole, feed, max_step, smooth_feed;
    unsigned length, capacity, mask, position[2], remaining;
    float *ring[2], phase[2][2], filtered_delay[2];
    smoother smooth[6];
    bool started, depth_active, enable_changed, skip;
    int fade, fade_length, disable_count;
    float old_stereo, old_noise, old_bias, old_mix, old_gain;
    float depth, depth2, rate[2], scale, noise, bias, noise_delta, bias_delta;
    float mix, gain, mix_delta, gain_delta;
    int block_disable;
};
static float clamp(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static float wrap(float x) {
    while (x >= 1) x += -1.0f;
    while (x < 0) x += 1.0f;
    return x;
}
/* cLFO::Sine 0x135330: preserve the polynomial and quadrant operation order. */
static float sine(float phase) {
    float x; bool negative = phase >= 0.5f;
    if (phase < 0.5f) x = phase < 0.25f ? 0.25f - phase : phase + -0.25f;
    else x = phase < 0.75f ? 0.75f - phase : phase + -0.75f;
    x *= x;
    float y = (57.34150695800781f * x + -19.564743041992188f) * x + 0.9988061785697937f;
    return y * (negative ? -1.0011951923370361f : 1.0011951923370361f);
}
static float smooth_value(retro_wobble *s, unsigned i, float target) {
    smoother *v = &s->smooth[i];
    target *= 16384.0f;
    if (v->initial) { v->initial = false; v->value = target; }
    else if (v->value != target) {
        float delta = (target - v->value) * s->smooth_feed;
        float sign = delta < 0 ? -1.0f : 1.0f;
        float distance = delta * sign;
        v->value = distance < 0.016383999958634377f ? target :
            v->value + sign * fminf(s->max_step, distance);
    }
    return v->value * 6.103515625e-05f;
}

retro_wobble_params retro_wobble_defaults(void) {
    return (retro_wobble_params){.wow_depth = 0.5f, .flutter_depth = 0.5f,
        .wow_rate = 0.2f, .flutter_rate = 0.294f, .mix = 1, .enable = 1};
}
retro_wobble *retro_wobble_create(void) {
    retro_wobble *s = calloc(1, sizeof(*s));
    if (s) s->p = retro_wobble_defaults();
    return s;
}
void retro_wobble_destroy(retro_wobble *s) {
    if (s) { free(s->ring[0]); free(s->ring[1]); free(s); }
}
int retro_wobble_set(retro_wobble *s, const retro_wobble_params *p) {
    if (!s || !p) return 0;
    const float normalized[] = {p->wow_depth, p->flutter_depth, p->wow_rate,
        p->flutter_rate, p->mix, p->noise_scale};
    for (unsigned i = 0; i < sizeof(normalized) / sizeof(*normalized); ++i)
        if (!isfinite(normalized[i]) || normalized[i] < 0 || normalized[i] > 1) return 0;
    if (!isfinite(p->stereo_phase) || p->stereo_phase < 0 || p->stereo_phase > 0.4f ||
        !isfinite(p->lfo_noise) || p->lfo_noise < -1 || p->lfo_noise > 1 ||
        (p->enable != 0 && p->enable != 1)) return 0;
    if (memcmp(&s->p, p, sizeof(*p))) {
        if (s->p.enable != p->enable) s->enable_changed = true;
        s->p = *p; s->remaining = 0;
    }
    return 1;
}
void retro_wobble_reset_phase(retro_wobble *s) {
    if (!s) return;
    for (unsigned c = 0; c < 2; ++c) {
        s->phase[c][0] = s->phase[c][1] = 0.75f;
        s->filtered_delay[c] = 0;
    }
    s->phase[1][0] = wrap(s->phase[1][0] + s->p.stereo_phase);
}
void retro_wobble_reset_smoothing(retro_wobble *s) {
    if (s && s->started) for (unsigned i = 0; i < 6; ++i) s->smooth[i].initial = true;
}
void retro_wobble_reset(retro_wobble *s) {
    if (!s) return;
    for (unsigned c = 0; c < 2; ++c) {
        if (s->ring[c]) memset(s->ring[c], 0, s->capacity * sizeof(float));
        s->position[c] = 0;
    }
    for (unsigned i = 0; i < 6; ++i) s->smooth[i] = (smoother){.initial = true};
    s->started = s->depth_active = s->skip = false;
    s->enable_changed = true;
    s->fade = s->disable_count = 0; s->remaining = 0;
    s->old_stereo = s->old_noise = s->old_bias = s->old_mix = s->old_gain = 0;
    retro_wobble_reset_phase(s);
}
int retro_wobble_prepare(retro_wobble *s, float sample_rate) {
    if (!s || !isfinite(sample_rate) || sample_rate < 1000 || sample_rate > 1536000) return 0;
    unsigned length = 2 * (unsigned)(sample_rate * 0.02f) + 1;
    unsigned capacity = 2;
    while (capacity < length) capacity *= 2;
    float *left = calloc(capacity, sizeof(float)), *right = calloc(capacity, sizeof(float));
    if (!left || !right) { free(left); free(right); return 0; }
    free(s->ring[0]); free(s->ring[1]); s->ring[0] = left; s->ring[1] = right;
    s->sample_rate = sample_rate; s->inverse_rate = 1.0f / sample_rate;
    s->length = length; s->capacity = capacity; s->mask = capacity - 1;
    s->pole = expf((10.0f / sample_rate) * -6.2831855f); s->feed = 1.0f - s->pole;
    s->max_step = ((sample_rate * 4.999999873689376e-06f) * 0.015625f) * 16384.0f;
    s->smooth_feed = 1.0f - expf((-3.1415927f * s->max_step) / 0.001f);
    s->fade_length = (int)((sample_rate * 0.5f) * 0.015625f);
    retro_wobble_reset(s);
    return 1;
}
static void restart_fade(retro_wobble *s) {
    s->fade = (s->depth != 0 ? -1 : 1) * s->fade_length;
    if (s->depth != 0) s->old_noise = s->old_bias = 0;
}
static void controls(retro_wobble *s) {
    if (s->enable_changed) {
        s->disable_count = s->p.enable < 0.5f ? 10 : 0;
        s->enable_changed = false;
    }
    s->remaining = 64;
    s->skip = s->p.enable < 0.5f && s->disable_count < 1;
    if (s->skip) { s->started = false; return; }
    s->depth = smooth_value(s, 0, retro_scale_fit3(s->p.wow_depth, 0, 0.8f, 0.4f));
    s->depth2 = smooth_value(s, 1, retro_scale_fit3(s->p.flutter_depth, 0, 0.2f, 0.1f));
    s->scale = smooth_value(s, 3, s->p.noise_scale);
    if (!s->started) {
        s->started = true;
        retro_wobble_reset_phase(s);
        restart_fade(s);
    }
    if ((s->depth != 0) != s->depth_active && s->fade == 0) restart_fade(s);
    s->rate[0] = s->inverse_rate * retro_scale_fit3(s->p.wow_rate, 0.1f, 4, 1);
    s->rate[1] = s->inverse_rate * retro_scale_fit3(s->p.flutter_rate, 6, 20, 10);
    float stereo = smooth_value(s, 2, s->p.stereo_phase);
    if (stereo != s->old_stereo) {
        s->phase[1][0] = wrap(stereo == 0 ? s->phase[0][0] :
            (stereo - s->old_stereo) * 0.5f + s->phase[1][0]);
        s->old_stereo = stereo;
    }
    float noise = s->p.lfo_noise * s->scale, ramp = 1;
    if (s->fade) {
        int numerator;
        if (s->fade > 0) numerator = --s->fade;
        else { numerator = s->fade + s->fade_length; ++s->fade; }
        ramp = (float)numerator / (float)s->fade_length;
        noise *= ramp;
    }
    float bias = ((0.8f - s->depth) * 0.5f) * ramp * s->scale;
    s->mix = smooth_value(s, 4, s->p.mix);
    (void)smooth_value(s, 5, s->depth + s->depth2 > 0 ? 1 : 0);
    /* Native scales the fixed-point boost cell after applying the 3 dB factor. */
    float gain_db, gain_base;
    if (s->mix >= 0.5f) {
        gain_base = (3.0f * s->smooth[5].value) * 6.103515625e-05f;
        gain_db = (s->mix + s->mix + -1.0f) * (0.0f - gain_base);
    } else {
        gain_db = (((s->mix + s->mix) * 3.0f) * s->smooth[5].value) * 6.103515625e-05f;
        gain_base = 0;
    }
    s->gain = (float)pow(10.0, (double)(gain_base + gain_db) / 20.0);
    s->noise = noise; s->bias = bias;
    s->noise_delta = (s->old_noise - noise) * 0.015625f;
    s->bias_delta = (s->old_bias - bias) * 0.015625f;
    s->mix_delta = (s->old_mix - s->mix) * 0.015625f;
    s->gain_delta = (s->old_gain - s->gain) * 0.015625f;
    s->block_disable = s->disable_count;
    if (s->disable_count > 0) --s->disable_count;
    s->old_noise = noise; s->old_bias = bias; s->old_mix = s->mix; s->old_gain = s->gain;
    s->depth_active = s->depth != 0;
}
static float interpolate(retro_wobble *s, unsigned c, float delay, bool linear) {
    unsigned integer = (unsigned)delay, at = s->position[c] - integer;
    float t = delay - (float)integer;
    float b = s->ring[c][at & s->mask], d = s->ring[c][(at - 1) & s->mask];
    if (linear || delay < 1) return t * (d - b) + b;
    float a = s->ring[c][(at + 1) & s->mask], e = s->ring[c][(at - 2) & s->mask];
    float q = ((b * -5.0f + (a + a)) + d * 4.0f) - e;
    float p = ((e - d * 3.0f) + b * 3.0f) - a;
    float y = (p * t + q) * t + (d - a);
    return y * (t * 0.5f) + b;
}
void retro_wobble_process(retro_wobble *s, const float *left, const float *right,
                          float *out_left, float *out_right, size_t frames) {
    if (!s || !s->ring[0]) return;
    for (size_t i = 0; i < frames; ++i) {
        if (!s->remaining) controls(s);
        float input[2] = {isfinite(left[i]) ? left[i] : 0, isfinite(right[i]) ? right[i] : 0};
        float output[2];
        if (s->skip) { output[0] = input[0]; output[1] = input[1]; }
        else {
            float reverse = (float)(s->remaining - 1);
            float noise = 0, bias = 0;
            if ((s->scale != 0 && s->depth != 0) || s->fade != 0) {
                noise = s->noise_delta * reverse + s->noise;
                bias = reverse * s->bias_delta + s->bias + (s->depth + 0.8f) * noise;
            }
            float mix = reverse * s->mix_delta + s->mix;
            float gain = reverse * s->gain_delta + s->gain;
            for (unsigned c = 0; c < 2; ++c) {
                float wow = sine(s->phase[c][0]);
                s->phase[c][0] = wrap(s->rate[0] + s->phase[c][0]);
                float flutter = sine(s->phase[c][1]);
                s->phase[c][1] = wrap(s->rate[1] + s->phase[c][1]);
                float unipolar = clamp((wow + 1.0f) * 0.5f * s->depth + bias, 0, 0.8f);
                unipolar = clamp((flutter + 1.0f) * 0.5f * s->depth2 + unipolar, 0, 1);
                float target = unipolar * (float)s->length + ((s->depth != 0 || s->depth2 != 0) ? 1 : 0);
                volatile float rounded = s->pole * s->filtered_delay[c] + target * s->feed + 0.0001f;
                float delay = rounded + -0.0001f;
                s->filtered_delay[c] = delay;
                s->position[c] = (s->position[c] + 1) & s->mask;
                s->ring[c][s->position[c]] = input[c];
                if (s->block_disable > 0) {
                    float low = (float)(s->block_disable - 1) / 10.0f;
                    float high = (float)s->block_disable / 10.0f;
                    float blend = ((high - low) * (float)s->remaining * 0.015625f + low) *
                        (s->mix_delta * 64.0f + s->mix);
                    output[c] = (interpolate(s, c, delay, true) * blend + input[c] * (1.0f - blend)) * gain;
                } else if (s->mix_delta != 0) {
                    output[c] = (interpolate(s, c, delay, false) * mix + input[c] * (1.0f - mix)) * gain;
                } else {
                    output[c] = (interpolate(s, c, delay, false) * s->mix + input[c] * (1.0f - s->mix)) * s->gain;
                }
            }
        }
        out_left[i] = output[0]; out_right[i] = output[1]; --s->remaining;
    }
}
