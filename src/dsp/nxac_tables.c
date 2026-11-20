#include "nxac_tables.h"
#include <math.h>

/* Original initialization at 0x1684f3..0x16851f performs double exp2,
   rounds to float, then takes the reciprocal in float. */
float rs_nxac_gain(uint8_t index) {
    double exponent = ((double)index + -30.0) / 10.0;
    float denominator = (float)exp2(exponent);
    return 1.0f / denominator;
}

/* The zero index is a sentinel, distinct from either signed exponential.
   The sign multiplier is applied in double before rounding to float. */
float rs_nxac_delta(uint8_t index) {
    if (index == 0) return 0.0f;
    int distance = index < 128 ? 128 - (int)index : (int)index - 128;
    double value = exp2((double)distance / 7.9375);
    return (float)(value * (index < 128 ? -0x1p-16 : 0x1p-16));
}
