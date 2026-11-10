/* RC-20 1.0.5 cAEMagicDistortion::tubePair setup 0xb1ab0/process 0xb2550. */
#include "tube_pair.h"
#include <math.h>

_Static_assert(sizeof(rs_tube_pair) == 148, "Original TubePair float layout");
static void coefficients(float *filter, float sample_rate, float cutoff, int high_pass) {
    float exponent = (float)(((double)cutoff * 6.2831852) / (double)sample_rate);
    float a = (float)exp((double)-exponent);
    filter[0] = a;
    if (high_pass) {
        filter[1] = (a + 1.0f) * 0.5f;
        filter[2] = -filter[1];
    } else {
        filter[1] = (1.0f - a) / 1.12f;
        filter[2] = filter[1] * 0.12f;
    }
}
void rs_tube_pair_setup(rs_tube_pair *s, float sample_rate, float amount) {
    float *v = s->state;
    v[35] = (float)((exp((double)amount * 3.0) * 30.0) * 0.01);
    v[36] = (float)(((double)amount + 1.0) / (double)v[35]);
    float limit = sample_rate * 0.499f;
    coefficients(v, sample_rate, fminf(limit, 25.0f), 1);
    coefficients(v + 7, sample_rate, fminf(limit, amount * 50.0f + 7.0f), 1);
    coefficients(v + 14, sample_rate, fminf(limit, amount * -5000.0f + 9000.0f), 0);
    coefficients(v + 21, sample_rate, fminf(limit, 50.0f), 0);
    coefficients(v + 28, sample_rate, fminf(limit, 50.0f), 0);
}
void rs_tube_pair_reset(rs_tube_pair *s) {
    for (unsigned filter = 0; filter < 5; ++filter)
        for (unsigned index = 3; index < 7; ++index) s->state[filter * 7 + index] = 0;
}
static float filter_sample(float *v, float input, unsigned channel, int flush) {
    float output = v[0] * v[3 + channel] + (v[2] * v[5 + channel] + v[1] * input);
    if (flush) {
        volatile float rounded = output + 1.0e-4f;
        output = rounded + -1.0e-4f;
    }
    v[3 + channel] = output;
    v[5 + channel] = input;
    return output;
}
static float shape(float input) {
    float output = -input;
    if (!(input > -0.0f)) output = (float)atan((double)output);
    return output;
}
float rs_tube_pair_process(rs_tube_pair *s, float sample, unsigned channel) {
    float *v = s->state;
    float input_gain = filter_sample(v + 21, v[35], channel, 0);
    float output_gain = filter_sample(v + 28, v[36], channel, 0);
    float first = shape(sample * input_gain) * output_gain;
    first = filter_sample(v, first, channel, 1);
    float second = shape(input_gain * first) * output_gain;
    second = filter_sample(v + 7, second, channel, 1);
    return filter_sample(v + 14, second, channel, 0);
}
