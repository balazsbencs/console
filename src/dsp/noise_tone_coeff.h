#ifndef RETRO_STUDY_NOISE_TONE_COEFF_H
#define RETRO_STUDY_NOISE_TONE_COEFF_H
/* Recovered Noise type-7 high shelf: 800 Hz, Q=.5. Inputs are positive
   sample rate and the already mapped/smoothed shelf gain in dB. Coefficients
   are b0,b1,b2,a1,a2 for y=b*x-a*y. No filter history is modified here. */
void rs_noise_tone_coeff(float rate, float shelf_db, float coeff[5], float *correction);
#endif
