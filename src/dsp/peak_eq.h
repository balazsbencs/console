#ifndef RS_PEAK_EQ_H
#define RS_PEAK_EQ_H
#include <stddef.h>
#include <stdint.h>
/* First160bytes preserve native cAEBiQuadPlug numeric layout for validation. */
typedef struct {
    uint64_t reserved;
    int32_t type;
    float rate, frequency, q, db;
    float coefficient[5], previous[5], target[5];
    int32_t maximum_interpolation, interpolation_left;
    float inverse_interpolation;
    float history[2][4]; /* x[n-2], x[n-1], y[n-2], y[n-1] */
    uint8_t bypass, desired_bypass, reserved86[2], fade_active, reserved89[3];
    int32_t fade_left, fade_frames;
    float inverse_fade;
    int32_t fade_direction;
    uint8_t reset_coefficients, reserved9d[3];
    unsigned batch_left, lane, batch_filter, batch_fade;
    float fade_start, fade_end, fade_weight;
} rs_peak_eq;
/* Zero initialize, set physical controls, then prepare. */
int rs_peak_eq_set(rs_peak_eq *s, float frequency, float q, float db);
int rs_peak_eq_prepare(rs_peak_eq *s, float sample_rate);
void rs_peak_eq_reset(rs_peak_eq *s);
void rs_peak_eq_process(rs_peak_eq *s, float *left, float *right, size_t frames);
#endif
