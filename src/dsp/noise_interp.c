#include "noise_interp.h"

float rs_noise_interpolate(float previous, float center, float next,
                           float following, float fraction) {
    /* Preserve the order in cResamplingAudioSupplier::ReadNextStereo. */
    float base = center * 6.0f;
    float linear = (next * 6.0f - following) - center * 3.0f;
    linear -= previous + previous;
    float quadratic = (previous + next) * 3.0f - base;
    float cubic = ((center - next) * 3.0f + following) - previous;
    return (((cubic * fraction + quadratic) * fraction + linear) * fraction + base)
           * .1666666716337204f;
}
