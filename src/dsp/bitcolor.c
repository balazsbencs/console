#include "bitcolor.h"
#include "curves.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

static float clamp(float x, float low, float high) {
    return x < low ? low : x > high ? high : x;
}
static float ramp(float x, float low, float high) {
    return clamp((x - low) / (high - low), 0.0f, 1.0f);
}
/* The original forces float rounding between these additions. Keep that order. */
static float denormal_round(float x) {
    volatile float rounded = x + 0.0001f;
    return rounded + -0.0001f;
}
static float pole(float cutoff, float sample_rate) {
    return expf(clamp(cutoff / sample_rate, 0.0f, 0.5f) * -6.2831855f);
}

retro_digital_params retro_digital_defaults(void) {
    return (retro_digital_params){.bits = 6.0f, .rate = 4000.0f, .smooth = 0.0f,
        .mix = 1.0f, .feedback = 0.0f, .low = 20.0f, .high = 20000.0f,
        .round_error = 0.01f, .stereo = 1.0f, .aa2 = 0.5f, .aa3 = 0.65f,
        .focus = 1.0f};
}

int retro_digital_set(retro_digital *dsp, const retro_digital_params *p) {
    if (!dsp || !p) return 0;
    const float values[] = {p->bits, p->rate, p->smooth, p->mix, p->feedback,
        p->low, p->high, p->round_error, p->stereo, p->aa2, p->aa3,
        p->focus, p->mod_rate, p->mod_depth, p->normalized};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
        if (!isfinite(values[i])) return 0;
    if ((p->normalized != 0 && p->normalized != 1) ||
        (p->normalized ? (p->bits < 0 || p->bits > 1 || p->rate < 0 || p->rate > 1) :
        (p->bits < 1 || p->bits > 24 || p->rate < 10 || p->rate > 44100)) ||
        p->low < 20 || p->low > 20000 || p->high < 20 || p->high > 20000 ||
        p->smooth < 0 || p->smooth > 1 || p->mix < 0 || p->mix > 1 ||
        p->feedback < 0 || p->feedback > 1 || p->round_error < 0 || p->round_error > 1 ||
        p->stereo < 0 || p->stereo > 1 || p->aa2 < 0 || p->aa2 > 1 || p->aa3 < 0 || p->aa3 > 1 ||
        (p->focus != 0 && p->focus != 1) || p->mod_rate < 0 || p->mod_rate > 1 || p->mod_depth < 0 || p->mod_depth > 1)
        return 0;
    if (p->normalized != dsp->params.normalized) dsp->rate_state = p->normalized ? p->rate : 0.5f;
    dsp->params = *p;
    /* Recalculate on the next sample when host automation changes a control. */
    dsp->control_remaining = 0;
    return 1;
}

void retro_digital_reset(retro_digital *dsp) {
    memset(dsp->channels, 0, sizeof(dsp->channels));
    for (unsigned c = 0; c < 2; ++c)
        for (unsigned i = 0; i < 32; ++i) dsp->channels[c].ring[i] = 1.0f;
    dsp->channels[1].phase = 0.5f;
    dsp->bits_state = dsp->params.normalized ? retro_scale_fit3(dsp->params.bits, 24, 1, 6) : dsp->params.bits;
    dsp->rate_state = dsp->params.normalized ? dsp->params.rate : 0.5f;
    dsp->gain_state = 1.0f;
    dsp->mod_phase = 0.0f;
    dsp->mix_state = dsp->params.mix;
    dsp->control_remaining = 0;
}

int retro_digital_prepare(retro_digital *dsp, float sample_rate) {
    if (!dsp || !isfinite(sample_rate) || sample_rate < 1 || sample_rate > 1536000) return 0;
    dsp->sample_rate = sample_rate;
    dsp->bits_pole = pole(320.0f, sample_rate); dsp->bits_feed = 1.0f - dsp->bits_pole;
    dsp->mix_pole = pole(10.0f, sample_rate); dsp->mix_feed = 1.0f - dsp->mix_pole;
    retro_digital_reset(dsp);
    return 1;
}

/* REA 0x345df0, with explicit physical and original normalized control modes.
 * Circular history, quantizer and post-smoothing follow original operation order. */
static void update_controls(retro_digital *dsp) {
    const retro_digital_params *p = &dsp->params;
    dsp->feedback_gain = 1.0f / (p->feedback * -0.77f + 1.0f);
    dsp->rate_state = denormal_round(dsp->bits_pole * dsp->rate_state +
        (p->normalized ? p->rate : 0.5f) * dsp->bits_feed);
    float rate = p->normalized ? retro_scale_fit3(dsp->rate_state, 44100, 44.1f, 200) : p->rate;
    float bits = p->normalized ? retro_scale_fit3(p->bits, 24, 1, 6) : p->bits;
    dsp->bits_state = denormal_round(dsp->bits_pole * dsp->bits_state + bits * dsp->bits_feed);
    dsp->step = 1.0f / exp2f(dsp->bits_state + -1.0f);
    float softness = powf(clamp(1.0f - powf(dsp->step, 0.57f), 0.0f, 1.0f), 1.5f);
    dsp->quant_gain = (1.0f - softness) * -0.8f + 1.0f;
    dsp->levels = 1.0f / dsp->step;
    dsp->rounding = 0.5f - p->round_error * 0.499f;
    dsp->blend_scale = 1.0f / (p->smooth + 0.00001f);

    float rate_scale = powf((((1.0f - p->mod_depth) * rate) * 32.0f) / dsp->sample_rate, 0.25f);
    float rate_high = powf((((p->mod_depth + 1.0f) * rate) * 32.0f) / dsp->sample_rate, 0.25f);
    float phase = (((p->mod_rate * 0.0005681818f) /
        (p->mod_rate * -0.99886364f + 1.0f + -0.0005681818f)) * 440.0f * 64.0f) /
        dsp->sample_rate + dsp->mod_phase;
    dsp->mod_phase = phase - floorf(phase);
    float centered = dsp->mod_phase + dsp->mod_phase + -1.0f;
    float angle = (fabsf(centered) + -0.5f) * 3.1415927f;
    angle *= angle;
    float wave = (angle * 0.03679168f + -0.49558073f) * angle + 0.9988062f;
    if (centered > 0.0f) wave = -wave;
    rate_scale = rate_scale + (rate_high - rate_scale) * (wave * 0.5f + 0.5f);
    float advance = rate_scale * rate_scale * rate_scale * rate_scale;
    dsp->advance = clamp(advance, 0.0f, 8.0f);
    float rate_ramp = ramp(rate, 20.0f, 190.0f);
    float aa_scale = p->smooth * ((rate_ramp * 0.8f) / (rate_ramp * 0.6f + 1.0f + -0.8f));
    float bit_ramp = ramp(dsp->bits_state, 1.0f, 4.0f);
    float aa = p->aa2 * (1.0f - bit_ramp) * aa_scale;
    dsp->slew_limit = aa >= 0.00001f ? 1.0f / (aa * aa * 0.008f * dsp->sample_rate) : 10000.0f;
    aa = 1.0f - p->aa3 * aa_scale * bit_ramp * (1.0f - ramp(dsp->bits_state, 4.0f, 10.0f));
    float cutoff = ((aa * 0.04f) / (aa * -0.92f + 1.0f + -0.04f)) * 19800.0f + 200.0f;
    /* This output filter is not Nyquist-clamped in the observed code. */
    dsp->output_feed = 1.0f - expf((cutoff / dsp->sample_rate) * -6.2831855f);
    dsp->wet_target = p->mix * (1.0f - ramp(rate, 20000.0f, 44098.0f) * ramp(dsp->bits_state, 21.0f, 23.5f));
    float low = p->low, high = p->high;
    dsp->gain_target = 1.0f;
    if (p->focus == 0) {
        float progress = p->normalized ? fminf(ramp(1.0f - p->rate, 0.5f, 1.0f),
            ramp(1.0f - p->bits, 0.5f, 1.0f)) : 0.0f;
        float low_raw = retro_log_normalize(low, 20, 20000);
        float high_raw = retro_log_normalize(high, 20, 20000);
        high_raw = (1.0f - high_raw) * progress + high_raw;
        low_raw = progress * (0.0f - low_raw) + low_raw;
        low = retro_scale_log(low_raw, 20, 20000);
        high = retro_scale_log(high_raw, 20, 20000);
        float width = (1.0f - high_raw) + low_raw;
        dsp->gain_target = width * width * 3.0f + 1.0f;
    }
    dsp->hp_pole = pole(low, dsp->sample_rate);
    dsp->hp_feed = (dsp->hp_pole + 1.0f) * 0.5f;
    dsp->lp_pole = pole(high, dsp->sample_rate);
    dsp->lp_feed = 1.0f - dsp->lp_pole;
    if (dsp->channels[0].phase < 0.49f)
        dsp->channels[1].phase = p->stereo * 0.5f + dsp->channels[0].phase;
    dsp->control_remaining = 64;
}

static float quantize(float input, float levels, float step, float gain, float rounding) {
    float centered = input + -1.0f;
    float value = (centered < 0.0f ? -rounding : rounding) + levels * centered;
    /* SSE cvttss2si returns INT32_MIN for invalid/out-of-range conversions. */
    int32_t integer = (!isfinite(value) || value >= 2147483648.0f || value < -2147483648.0f)
        ? INT32_MIN : (int32_t)value;
    return (float)integer * step * gain;
}

static float process_channel(retro_digital *dsp, retro_digital_channel *c, float input, float mix) {
    float boosted = input * dsp->gain_state;
    float high = denormal_round(c->hp_output * dsp->hp_pole + (boosted - c->hp_input) * dsp->hp_feed);
    c->hp_input = boosted; c->hp_output = high;
    if (dsp->params.focus == 0) {
        float high2 = denormal_round(dsp->hp_pole * c->hp2_output + dsp->hp_feed * (high - c->hp2_input));
        c->hp2_input = high; c->hp2_output = high2; high = high2;
    }
    float band = denormal_round(c->lp_output * dsp->lp_pole + high * dsp->lp_feed);
    c->lp_output = band;
    if (dsp->params.focus == 0) {
        band = denormal_round(dsp->lp_pole * c->lp2_output + dsp->lp_feed * band);
        c->lp2_output = band;
    }
    c->phase = c->phase + dsp->advance;
    unsigned count = (unsigned)c->phase;
    if (count) {
        float target = dsp->feedback_gain * band + 1.0f;
        for (unsigned i = 0; i < count; ++i) {
            unsigned index = (c->position + i + 1) & 31;
            c->ring[index] = (c->ring[index] - target) * dsp->params.feedback + target;
        }
        c->position = (c->position + count) & 31;
        c->phase = c->phase - (float)count;
    }
    float previous = quantize(c->ring[(c->position + 31) & 31], dsp->levels, dsp->step, dsp->quant_gain, dsp->rounding);
    float current = quantize(c->ring[c->position], dsp->levels, dsp->step, dsp->quant_gain, dsp->rounding);
    float value = (current - previous) * clamp(c->phase * dsp->blend_scale, 0.0f, 1.0f) + previous;
    if (fabsf(value) < 1e-9f) value = 0.0f;
    float delta = value - c->slew;
    float sign = delta < 0 ? -1.0f : 1.0f;
    float distance = delta * sign;
    if (distance >= 1e-6f) value = c->slew + fminf(distance, dsp->slew_limit) * sign;
    c->slew = value;
    c->filtered = (value - c->filtered) * dsp->output_feed + c->filtered;
    return (((input - band) * dsp->params.focus + c->filtered) - input) * mix + input;
}

void retro_digital_process(retro_digital *dsp, const float *left, const float *right,
                           float *out_left, float *out_right, size_t frames) {
    for (size_t i = 0; i < frames; ++i) {
        if (!dsp->control_remaining) update_controls(dsp);
        dsp->mix_state = denormal_round(dsp->mix_pole * dsp->mix_state + dsp->mix_feed * dsp->wet_target);
        dsp->gain_state = denormal_round(dsp->mix_pole * dsp->gain_state + dsp->mix_feed * dsp->gain_target);
        float l = isfinite(left[i]) ? left[i] : 0.0f;
        float r = isfinite(right[i]) ? right[i] : 0.0f;
        out_left[i] = process_channel(dsp, &dsp->channels[0], l, dsp->mix_state);
        out_right[i] = process_channel(dsp, &dsp->channels[1], r, dsp->mix_state);
        --dsp->control_remaining;
    }
}
