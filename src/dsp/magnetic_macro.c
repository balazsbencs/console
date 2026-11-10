#include "magnetic_macro.h"
#include "relations.h"
static float dropout_amount(float magnitude, float amount, float dropouts) {
    float a = magnitude * amount;
    float mapped = rs_relation_from_to(a, 0, 1, 0, 1, .98f);
    return dropouts * (mapped * magnitude);
}
void rs_magnetic_macro_reset(rs_magnetic_macro *s, int32_t depth_seed,
    int32_t dropout_seed, int32_t wear_seed, int32_t pan_seed) {
    rs_noisegen_reset(&s->depth, depth_seed);
    rs_noisegen_reset(&s->dropout, dropout_seed);
    rs_noisegen_reset(&s->wear, wear_seed);
    rs_noisegen_reset(&s->pan, pan_seed);
    s->depth_output=s->dropout_output=s->wear_output=s->pan_output=0;
}
void rs_magnetic_macro_tick(rs_magnetic_macro *s, float sample_rate,
    float magnitude, float flux, float amount, float dropouts) {
    float group = rs_relation_mul_triple(magnitude, flux, amount, .5f, .5f, .99f);
    float d = dropout_amount(magnitude, amount, dropouts);
    float rate = sample_rate * .015625f;
    float depth_gain = rs_relation_from_to_knob(group, .14f, .17f, .5f, .5f, .5f);
    float depth_smooth = rs_relation_from_to(group, 0, 1, .83f, .5f, .5f);
    float wear_smooth = rs_relation_from_to(group, 0, 1, .91f, .4f, .5f);
    float dropout_smooth = rs_relation_from_to(d, 0, 1, .3f, .05f, .15f);
    rs_noisegen_set_params(&s->depth, depth_gain, 0, depth_smooth, .5f, true, rate);
    rs_noisegen_set_params(&s->dropout, .87837785f, .424324f, dropout_smooth, .656756758f, false, rate);
    rs_noisegen_set_params(&s->wear, .44f, 0, wear_smooth, .5f, true, rate);
    rs_noisegen_set_params(&s->pan, .40f, 0, .837f, .5f, true, rate);
    s->depth_output=rs_noisegen_get_value(&s->depth)*16.0f;
    s->dropout_output=rs_noisegen_get_value(&s->dropout)*16.0f;
    s->wear_output=rs_noisegen_get_value(&s->wear)*16.0f;
    s->pan_output=rs_noisegen_get_value(&s->pan)*16.0f;
}
void rs_magnetic_macro_apply(const rs_magnetic_macro *s, float magnitude,
    float amount, float balance, float dropouts, retro_magnetic_params *p) {
    float wear, flutter;
    rs_relation_triangle(balance, &wear, &flutter);
    float a = magnitude * amount;
    p->depth = flutter * a;
    p->wear_depth = wear * a;
    p->depth_steer=rs_relation_modulate(.5f,s->depth_output,.85f);
    p->dropout_steer=rs_relation_modulate(0,s->dropout_output,.75f);
    p->wear_gain=rs_relation_modulate(0,s->wear_output,.85f);
    p->wear_pan=rs_relation_modulate(0,s->pan_output,.85f);
    float d=dropout_amount(magnitude,amount,dropouts);
    p->dropout_amount=d;
    p->dropout_min=rs_relation_from_to(d,0,1,.1f,.68f,.5f);
    p->dropout_max=rs_relation_from_to(d,0,1,.25f,.88f,.5f);
}
