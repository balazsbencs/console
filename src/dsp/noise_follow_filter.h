#ifndef RETRO_STUDY_NOISE_FOLLOW_FILTER_H
#define RETRO_STUDY_NOISE_FOLLOW_FILTER_H
/* Native Follow prefilter, BiQuad type2 high-pass with Q=.707f. Input cutoff
   is physical Hz, sample rate positive. Output order b0,b1,b2,a1,a2 for
   y=b*x-a*y. This helper does not change filter history. */
void rs_noise_follow_filter(float rate, float cutoff_hz, float coeff[5]);
#endif
