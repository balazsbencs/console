#include "noise_follow_filter.h"
#include <math.h>

/* cNoiseFXBlock::processAudioInternal at0x3a5be3..0x3a5bff passes
   cutoff/rate, Q=.707f, gain0 to BiQuad Params::Setup(type2). */
void rs_noise_follow_filter(float rate, float cutoff_hz, float coeff[5]) {
    float frequency = cutoff_hz / rate;
    frequency = frequency <= 0.49f ? frequency : 0.49f;
    float omega = frequency * 6.2831855f;
    float alpha = (sinf(omega) * 0.5f) / 0.707f;
    float cosine = cosf(omega);
    float a1 = cosine * -2.0f;
    float a2 = 1.0f - alpha;
    float b1 = -1.0f - cosine;
    float b0 = b1 * -0.5f;
    float denominator = alpha + 1.0f;
    float inverse = 1.0f / denominator;
    coeff[0] = b0 * inverse;
    coeff[1] = b1 * inverse;
    coeff[2] = b0 * inverse;
    coeff[3] = a1 * inverse;
    coeff[4] = inverse * a2;
}
