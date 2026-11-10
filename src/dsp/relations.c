/* Recovered from cRelationSpecHandler::objectChanged, RC-20 1.0.5 0xf0d70. */
#include "relations.h"
static float upper(float x, float hi) { return x <= hi ? x : hi; }
static float clamp(float x, float lo, float hi) { return x < lo ? lo : upper(x, hi); }
static float curve(float x, float k) {
    if (k == 0.5f) return x;
    float denominator = (k + k + -1.0f) * x;
    float numerator = x * k;
    denominator = denominator + 1.0f;
    denominator = denominator - k;
    return numerator / denominator;
}
float rs_relation_from_to(float input, float from_low, float from_high,
                          float to_low, float to_high, float k) {
    float x = clamp((input - from_low) / (from_high - from_low), 0, 1);
    float y = (x * k) / (((k + k + -1.0f) * x + 1.0f) - k);
    return (to_high - to_low) * y + to_low;
}
float rs_relation_from_to_knob(float input, float low, float middle, float high,
                               float lower_curve, float upper_curve) {
    float from, to, k, x = input;
    if (input < 0.5f) {
        from = low; to = middle; k = lower_curve;
    } else {
        from = middle; to = high; k = upper_curve;
        x = x + -0.5f;
    }
    x = x + x;
    float numerator = x * k;
    float denominator = (k + k + -1.0f) * x;
    denominator = denominator + 1.0f;
    denominator = denominator - k;
    float y = numerator / denominator;
    return (to - from) * y + from;
}
void rs_relation_triangle(float x, float *left, float *right) {
    float inv = 1.0f - x;
    *left = x < 0.5f ? 1.0f : inv + inv;
    *right = x > 0.5f ? 1.0f : x + x;
}
float rs_relation_mul_triple(float a, float b, float c, float ka, float kb, float kc) {
    float ab = curve(a, ka) * curve(b, kb);
    return ab * curve(c, kc);
}
float rs_relation_modulate(float input, float mod, float style) {
    if (style <= 0.25f) return clamp(input * (mod + 1.0f), 0.0f, 1.0f);
    if (style <= 0.5f) return clamp(input * (1.0f - mod), 0.0f, 1.0f);
    if (style <= 0.6f) return clamp(input * mod, 0.0f, 1.0f);
    if (style <= 0.75f) return clamp(input + mod, 0.0f, 1.0f);
    if (style <= 0.85f) return clamp(input + mod, -1.0f, 1.0f);
    if (style <= 0.9f) return clamp(input - mod, input * 0.1f, input);
    if (style <= 0.95f) {
        float x = clamp(mod + 1.0f, 0.0f, 1.0f) * input;
        return clamp(x, 0.0f, input);
    }
    return clamp(input - mod, 0.0f, 1.0f);
}
