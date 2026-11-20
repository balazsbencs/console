#include "noise_tone_coeff.h"
#include <math.h>

/* BiQuad Params::Setup 0x1a4ff0 caches alpha/cos; ChangeGain type7 at
   0x1a5761..0x1a5866 uses the arithmetic below. Keep FP contraction off. */
void rs_noise_tone_coeff(float rate, float shelf_db, float coeff[5], float *correction) {
    float frequency = 800.0f / rate;
    frequency = frequency <= 0.49f ? frequency : 0.49f;
    float omega = frequency * 6.2831855f;
    float alpha = (sinf(omega) * 0.5f) / 0.5f;
    float cosine = cosf(omega);
    float exponent = shelf_db * 0.025f;
    exponent = exponent * 2.3025851f;
    float gain = expf(exponent);
    float beta = sqrtf(gain) * alpha;
    beta = beta + beta;
    float minus = -1.0f + gain;
    float plus = 1.0f + gain;
    float cosine_plus = plus * cosine;
    float a1 = minus - cosine_plus;
    a1 = a1 + a1;
    float cosine_minus = cosine * minus;
    float denominator = plus - cosine_minus;
    float a2 = denominator - beta;
    float center = cosine_minus + plus;
    float b0 = (beta + center) * gain;
    float b1_scale = -2.0f * gain;
    float b1 = (cosine_plus + minus) * b1_scale;
    float b2 = (center - beta) * gain;
    denominator = denominator + beta;
    float inverse = 1.0f / denominator;
    coeff[0] = b0 * inverse;
    coeff[1] = b1 * inverse;
    coeff[2] = b2 * inverse;
    coeff[3] = a1 * inverse;
    coeff[4] = inverse * a2;
    float correction_exponent = shelf_db * -0.5f;
    correction_exponent = correction_exponent * 0.05f;
    *correction = powf(10.0f, correction_exponent);
}
