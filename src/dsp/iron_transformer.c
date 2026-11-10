#include "iron_transformer.h"
#include <math.h>
#include <stddef.h>

static float pole(float cutoff, float sr) {
    float angle = (float)(((double)cutoff * 6.2831852) / (double)sr);
    return (float)exp((double)-angle);
}
static void lowpass(float *p, float coefficient) {
    p[0] = coefficient;
    p[1] = (1.0f - coefficient) / 1.12f;
    p[2] = p[1] * 0.12f;
}
int rs_iron_transformer_setup(rs_iron_transformer *s, float sr, float amount) {
    if (!s || !isfinite(sr) || sr < 1000.0f || sr > 1536000.0f ||
        !isfinite(amount) || amount < 0.0f || amount > 1.0f) return 0;
    float *v = s->state;
    float limit = sr * 0.499f;
    lowpass(v + 7, pole(fminf(limit, -12000.0f * amount + 20000.0f), sr));
    float cutoff = 350.0f * amount + 5.0f;
    float db = 9.0f * amount;
    float gain = (float)pow(10.0, (double)db / -20.0);
    float high = cutoff / gain;
    if (db < 0.0f) high *= gain;
    float low = db < 0.0f ? cutoff * gain : cutoff;
    float a = pole(fminf(limit, high), sr);
    float b = pole(fminf(limit, low), sr);
    v[14] = b;
    float factor = (float)(((1.0 - (double)b) * (double)(-1.0f / gain)) /
                           (double)fabsf(-1.0f + a));
    v[15] = -factor;
    v[16] = a * factor;
    float smooth = pole(fminf(limit, 50.0f), sr);
    lowpass(v + 21, smooth);
    lowpass(v + 28, smooth);
    lowpass(v + 35, smooth);
    float half = amount * 0.5f;
    float amplitude = (float)((exp(4.0 * (double)half) * 3.0) * 0.01);
    float ratio = (float)(((double)half + 1.2) / (double)amplitude);
    float time = sr / 44100.0f;
    float exponent = (half * 50.0f + 1.0f) / 44100.0f;
    float base = fmaxf(0.0f, 1.0f - (float)pow(0.5, (double)exponent));
    v[6] = 1.0f - powf(base, time);
    v[4] = amplitude / time;
    v[5] = time * ratio;
    return 1;
}
void rs_iron_transformer_reset(rs_iron_transformer *s) {
    if (!s) return;
    s->state[0] = s->state[1] = s->state[2] = s->state[3] = 0.0f;
    for (size_t i = 7; i <= 35; i += 7)
        for (size_t j = 3; j < 7; ++j) s->state[i + j] = 0.0f;
}
static float filter(float *p, float input, unsigned c) {
    float result = (p[1] * input + p[5 + c] * p[2]) + p[3 + c] * p[0];
    p[3 + c] = result;
    p[5 + c] = input;
    return result;
}
float rs_iron_transformer_process(rs_iron_transformer *s, float input, unsigned c) {
    if (!s || c > 1) return input;
    float *v = s->state;
    float pre = filter(v + 21, v[4], c);
    float attack = filter(v + 35, v[6], c);
    float integrated = (pre * input + v[c]) * attack;
    v[c] = integrated;
    float shaped = (float)atan((double)integrated);
    float difference = shaped - v[2 + c];
    v[2 + c] = shaped;
    float post = filter(v + 28, v[5], c);
    return filter(v + 14, filter(v + 7, post * difference, c), c);
}

void rs_iron_transformer_process_stereo(rs_iron_transformer *s, float *left, float *right) {
    if (!s || !left || !right) return;
    *left = rs_iron_transformer_process(s, *left, 0);
    *right = rs_iron_transformer_process(s, *right, 1);
}
