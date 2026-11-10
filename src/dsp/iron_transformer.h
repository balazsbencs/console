#ifndef RS_IRON_TRANSFORMER_H
#define RS_IRON_TRANSFORMER_H
/* Numerical Iron transformer core; no focus splitting, oversampling or outer mix. */
typedef struct { float state[42]; } rs_iron_transformer;
int rs_iron_transformer_setup(rs_iron_transformer *s, float sample_rate, float amount);
void rs_iron_transformer_reset(rs_iron_transformer *s);
float rs_iron_transformer_process(rs_iron_transformer *s, float input, unsigned channel);
/* One pair using the arithmetic order of original ShapeStereo2. */
void rs_iron_transformer_process_stereo(rs_iron_transformer *s, float *left, float *right);
#endif
