#include "curves.h"
#include <math.h>

float retro_scale_fit3(float x, float low, float high, float middle) {
    float range = high - low;
    float ratio = (middle - low) / range;
    return ((ratio * x) / ((ratio + ratio + -1.0f) * x + 1.0f - ratio)) * range + low;
}
float retro_scale_log(float x, float low, float high) {
    return expf(logf(high / low) * x) * low;
}
float retro_log_normalize(float value, float low, float high) {
    float x = logf(value / low) / logf(high / low);
    return x < 0 ? 0 : x > 1 ? 1 : x;
}
