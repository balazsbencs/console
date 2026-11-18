#include "space_macro.h"
#include "relations.h"

void rs_space_macro_reset(rs_space_macro *s, int32_t seed) {
    rs_noisegen_reset(&s->generator, seed);
    s->output = 0;
}
void rs_space_macro_tick(rs_space_macro *s, float sample_rate) {
    rs_noisegen_set_params(&s->generator, .5f, .5f, .42f, .59f, false,
                           sample_rate * .015625f);
    s->output = rs_noisegen_get_value(&s->generator) * 16.0f;
}
void rs_space_macro_apply(const rs_space_macro *s, float magnitude, float amount,
                          float flux, retro_space_params *params) {
    params->level = magnitude * amount;
    params->pre_wobble_amount = rs_relation_mul_triple(magnitude, flux, amount,
                                                      .5f, .5f, .99f);
    params->pre_wobble_noise = rs_relation_modulate(0, s->output, .75f);
}
