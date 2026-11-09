/* RC-20 1.0.5 isolated NoiseGen primitives: see analysis/re/noisegen-*. */
#include "noisegen.h"
#include <math.h>
#include <stddef.h>
#include <string.h>
_Static_assert(sizeof(rs_noisegen) == 0x28, "noise state layout");
_Static_assert(offsetof(rs_noisegen, decay_coefficient) == 0x18, "noise coefficients");
static float upper_one(float x) { return x <= 1.0f ? x : 1.0f; }
static float clamp01(float x) { return x < 0.0f ? 0.0f : upper_one(x); }
static float clamp11(float x) { return x < -1.0f ? -1.0f : upper_one(x); }
static float coefficient(float x, float rate) {
    float numerator = x * 0.9999f;
    float denominator = x * 0.99979997f + 1.0f;
    denominator = denominator + -0.9999f;
    float cutoff = 20000.0f - (numerator / denominator) * 20000.0f;
    return 1.0f - expf((cutoff / rate) * -6.2831855f);
}
void rs_noisegen_reset(rs_noisegen *s, int32_t seed) {
    s->envelope = s->smooth1 = s->smooth2 = 0.0f;
    s->rng_x = UINT32_C(0x67452301);
    s->rng_y = (uint32_t)seed * UINT32_C(0x401) ^ UINT32_C(0x90325477);
}
void rs_noisegen_set_params(rs_noisegen *s, float gain, float chaos,
                            float smooth, float decay, bool bipolar, float rate) {
    s->bipolar = (uint8_t)bipolar;
    float sc = coefficient(smooth, rate);
    s->smooth_coefficient = sc;
    s->decay_coefficient = coefficient(decay, rate);
    double inv_chaos = (double)(1.0f - chaos);
    float shape_scale = (float)(pow(inv_chaos, 8.0) * 0.5);
    float chaos6 = chaos * 6.0f;
    float shaped = powf(chaos6 < 0.0f ? 1.0f : 1.0f - upper_one(chaos6), 3.0f);
    s->shape = ((689.0625f / rate + -1.0f) * (1.0f - shaped) + 1.0f) * shape_scale;
    float gain_scale = clamp01((float)(pow(inv_chaos, -1.7000000476837158) * 0.0625));
    float correction = (float)(pow((double)(1.0f - decay), 3.0) * (double)(chaos * smooth));
    if (bipolar) {
        float numerator = correction * 11.0f + 1.0f;
        float p = (float)pow((double)smooth, 0.05000000074505806);
        float cube = (smooth * smooth) * smooth;
        float denominator = (0.02f - sc) * cube;
        denominator = denominator + sc;
        denominator = denominator + -1.0f;
        denominator = denominator * p + 1.0f;
        correction = numerator / denominator;
    } else {
        correction = correction * 21.0f + 1.0f;
    }
    s->gain = (gain_scale * correction) * gain;
}
float rs_noisegen_get_value(rs_noisegen *s) {
    s->rng_x ^= s->rng_y;
    s->rng_y += s->rng_x;
    int32_t signed_rng;
    memcpy(&signed_rng, &s->rng_y, sizeof(signed_rng));
    float random = (float)signed_rng;
    float v, k = s->shape;
    if (s->bipolar) {
        random = (random + random) * 2.3283064e-10f;
        float shaped = (random * k) / (((k + k + -1.0f) * random + 1.0f) - k);
        shaped = clamp11(shaped);
        v = shaped < 0.0f ? -shaped : shaped;
        if (v < s->envelope) v = (v - s->envelope) * s->decay_coefficient + s->envelope;
        s->envelope = v;
        v = (shaped < 0.0f ? -1.0f : 1.0f) * v;
    } else {
        random = random * 2.3283064e-10f + 0.5f;
        v = clamp01((random * k) / (((k * 2.0f + -1.0f) * random + 1.0f) - k));
        if (v < s->envelope) v = (v - s->envelope) * s->decay_coefficient + s->envelope;
        s->envelope = v;
    }
    s->smooth1 = (v - s->smooth1) * s->smooth_coefficient + s->smooth1;
    s->smooth2 = (s->smooth1 - s->smooth2) * s->smooth_coefficient + s->smooth2;
    return clamp11(s->smooth2 * s->gain);
}
