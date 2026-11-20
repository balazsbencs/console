#ifndef RS_NOISE_BIQUAD_H
#define RS_NOISE_BIQUAD_H

/* This direct-form filter stores the rounded recursive output before
   applying any gain outside the biquad. */
static inline float rs_noise_biquad_sample(const float c[5], float history[4], float input) {
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

#endif
