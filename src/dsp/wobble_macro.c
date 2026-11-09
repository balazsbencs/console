#include "wobble_macro.h"
#include "relations.h"

static float scaled_flux(float magnitude, float flux, float amount) {
    return rs_relation_mul_triple(magnitude, flux, amount, 0.5f, 0.5f, 0.99f);
}
void rs_wobble_macro_reset(rs_wobble_macro *s, int32_t wow_seed, int32_t flutter_seed) {
    rs_noisegen_reset(&s->wow, wow_seed);
    rs_noisegen_reset(&s->flutter, flutter_seed);
    s->wow_output = s->flutter_output = 0;
}
void rs_wobble_macro_tick(rs_wobble_macro *s, float sample_rate,
                          float magnitude, float flux, float amount) {
    float scaled = scaled_flux(magnitude, flux, amount);
    float rate = sample_rate * 0.015625f;
    rs_noisegen_set_params(&s->wow, scaled * 0.18918f,
        0.43783780f, 0.90f, 0.9054f, true, rate);
    rs_noisegen_set_params(&s->flutter, scaled * 0.2f,
        0.43783f, 0.8324f, 0.986486f, true, rate);
    s->wow_output = rs_noisegen_get_value(&s->wow) * 16.0f;
    s->flutter_output = rs_noisegen_get_value(&s->flutter) * 16.0f;
}
void rs_wobble_macro_apply(const rs_wobble_macro *s, float magnitude, float flux,
                           float amount, float balance, float stereo,
                           retro_wobble_params *p) {
    float wow, flutter;
    rs_relation_triangle(balance, &wow, &flutter);
    float scaled_amount = magnitude * amount;
    p->wow_depth = wow * scaled_amount;
    p->flutter_depth = rs_relation_modulate(flutter * scaled_amount, s->flutter_output, 0.95f);
    p->lfo_noise = rs_relation_modulate(0, s->wow_output, 0.85f);
    p->noise_scale = scaled_flux(magnitude, flux, amount) * wow;
    p->stereo_phase = rs_relation_from_to(stereo, 0, 1, 0, 0.4f, 0.5f);
}
