#ifndef RETRO_STUDY_RELATIONS_H
#define RETRO_STUDY_RELATIONS_H
/* Native relation primitives, finite controls; no implicit input clamping. */
void rs_relation_triangle(float balance, float *left, float *right);
float rs_relation_mul_triple(float a, float b, float c,
                             float curve_a, float curve_b, float curve_c);
float rs_relation_modulate(float input, float modulation, float style);
float rs_relation_from_to(float input, float from_low, float from_high,
                          float to_low, float to_high, float curvature);
#endif
