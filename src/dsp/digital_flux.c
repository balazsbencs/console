#include "digital_flux.h"
#include "relations.h"

void rs_digital_flux_reset(rs_digital_flux *s, int32_t bit_seed, int32_t rate_seed) {
    rs_noisegen_reset(&s->bits, bit_seed);
    rs_noisegen_reset(&s->rate, rate_seed);
    s->bit_output = s->rate_output = 0;
}

void rs_digital_flux_tick(rs_digital_flux *s, float sample_rate,
                          float magnitude, float flux, float amount) {
    float scaled = rs_relation_mul_triple(magnitude, flux, amount, 0.5f, 0.5f, 0.99f);
    float smooth = rs_relation_from_to(scaled, 0, 1, 0.9484f, 0.85f, 0.5f);
    float rate = sample_rate * 0.015625f;
    rs_noisegen_set_params(&s->bits, scaled * 0.3f, 0, 0.94847450187182f, 0, true, rate);
    rs_noisegen_set_params(&s->rate, scaled * 0.2f, 0, smooth, 0, true, rate);
    s->bit_output = rs_noisegen_get_value(&s->bits) * 16.0f;
    s->rate_output = rs_noisegen_get_value(&s->rate) * 16.0f;
}

void rs_digital_flux_targets(const rs_digital_flux *s, float magnitude,
                             float amount, float balance, float *bits, float *rate) {
    float bit_weight, rate_weight;
    float scaled = magnitude * amount;
    rs_relation_triangle(balance, &rate_weight, &bit_weight);
    *bits = rs_relation_modulate(bit_weight * scaled, s->bit_output, 0.75f);
    *rate = rs_relation_modulate(rate_weight * scaled, s->rate_output, 0.75f);
}
