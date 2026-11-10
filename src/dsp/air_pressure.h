#ifndef RETRO_AIR_PRESSURE_H
#define RETRO_AIR_PRESSURE_H
#include <stdint.h>
typedef struct {
    float state[36];
    float *ring[2];
    uint32_t mask, position[2];
    float delay, inverse_delay, sample_rate;
} rs_air_pressure;
int rs_air_pressure_prepare(rs_air_pressure *s, float sample_rate, float amount);
void rs_air_pressure_setup(rs_air_pressure *s, float sample_rate, float amount);
void rs_air_pressure_reset(rs_air_pressure *s);
void rs_air_pressure_destroy(rs_air_pressure *s);
float rs_air_pressure_process(rs_air_pressure *s, float input, unsigned channel);
#endif
