#include "space_internal.h"
#include <math.h>
#include <stdlib.h>
static const size_t offsets[14] = {0x108, 0x140, 0x178, 0x218, 0x2b8, 0x2f0, 0x328,
                                   0x360, 0x398, 0x3d0, 0x408, 0x440, 0x4e0, 0x518};
static float clamp(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float get(retro_space *s, size_t o) {
    SPACE_B(s, o + 0x2c) = 1;
    return SPACE_F(s, o + 0x30);
}
retro_space_params retro_space_defaults(void) {
    return (retro_space_params){1, .8f, 0, 22050, .15f, 0, 0, 0, 1, 1, 1, .01f, 0, 0};
}
int retro_space_set(retro_space *s, const retro_space_params *p) {
    if (!s || !p)
        return 0;
    float v[14];
    memcpy(v, p, sizeof(v));
    for (unsigned i = 0; i < 14; i++) {
        float hi = (i == 2 || i == 3) ? 22050 : i == 11 ? 100 : 1, lo = i == 12 ? -1 : 0;
        if (!isfinite(v[i]) || v[i] < lo || v[i] > hi)
            return 0;
        if ((i == 0 || i == 6 || i == 7 || i == 8 || i == 9 || i == 10) && v[i] != 0 && v[i] != 1)
            return 0;
    }
    s->p = *p;
    return 1;
}
static void cache(retro_space *s) {
    float v[14];
    memcpy(v, &s->p, sizeof(v));
    for (unsigned i = 0; i < 14; i++) {
        size_t o = offsets[i];
        int scaled = i == 2 || i == 3 || i == 11;
        size_t value = o + (scaled ? 0x9c : 0x30), dirty = o + (scaled ? 0x94 : 0x2c);
        if (SPACE_F(s, value) != v[i])
            SPACE_B(s, dirty) = 0;
        SPACE_F(s, value) = v[i];
        SPACE_B(s, dirty + 1) = 1;
    }
}
static void invalidate_controls(retro_space *s) {
    for (unsigned i = 0; i < 14; i++) {
        size_t o = offsets[i];
        SPACE_B(s, o + ((i == 2 || i == 3 || i == 11) ? 0x94 : 0x2c)) = 0;
    }
}
retro_space *retro_space_create(void) {
    retro_space *s = calloc(1, sizeof(*s));
    if (s)
        s->p = retro_space_defaults();
    return s;
}
void retro_space_destroy(retro_space *s) {
    if (s) {
        rs_space_free_state(s);
        free(s);
    }
}
void retro_space_reset_smoothing(retro_space *s) {
    if (!s)
        return;
    cache(s);
    float x = SPACE_F(s, 0x4dc) * .001f * s->rate;
    SPACE_F(s, 0x1138) = SPACE_F(s, 0x1134) = x;
    SPACE_B(s, 0x610) = 1;
}
int retro_space_prepare(retro_space *s, float rate) {
    if (!s || !isfinite(rate) || rate < 1000 || rate > 1536000)
        return 0;
    cache(s);
    if (!rs_space_prepare_state(s, rate))
        return 0;
    s->rate = rate;
    invalidate_controls(s);
    s->remaining = 0;
    return 1;
}
void retro_space_reset(retro_space *s) {
    if (s && s->rate) {
        cache(s);
        rs_space_rewind_state(s);
        invalidate_controls(s);
        s->remaining = 0;
    }
}
/* cDelay::SetLength 0x1ba7d0 and numerical Reset 0x1c1670/0x3614b0. */
static void length(retro_space *s, size_t b, int n) {
    int cap = (int)SPACE_U(s, b + 8);
    if (n < 1)
        n = 1;
    if (n > cap)
        n = cap;
    SPACE_U(s, b + 12) = (uint32_t)n;
    float *p = rs_space_pointer(s, b + 16);
    rs_space_store_pointer(s, b + 24, p + n - 1);
    rs_space_store_pointer(s, b + 32, p);
    memset(p, 0, (size_t)n * 4);
}
/* cLFO::Sine 0x135330; preserve quadrant/polynomial rounding. */
static float sine(float phase) {
    float x;
    int neg = phase >= .5f;
    if (phase < .5f)
        x = phase < .25f ? .25f - phase : phase + -.25f;
    else
        x = phase < .75f ? .75f - phase : phase + -.75f;
    x *= x;
    return ((57.34150695800781f * x + -19.564743041992188f) * x + .9988061785697937f) *
           (neg ? -1.0011951923370361f : 1.0011951923370361f);
}
/* The original left/right cubic tails multiply fraction/2 in different orders. */
static float cubic(retro_space *s, unsigned c, float delay) {
    size_t b = 0x5e0 + c * 16;
    float *p = rs_space_pointer(s, b);
    unsigned mask = SPACE_U(s, b + 8), i = SPACE_U(s, b + 12) - (int)delay;
    float f = delay - (float)(int)delay, a = p[(i + 1) & mask], b0 = p[i & mask],
          d = p[(i - 1) & mask], e = p[(i - 2) & mask];
    float q = (b0 * 3 + (e - d * 3)) - a;
    float r = (d * 4 + (b0 * -5 + (a + a))) - e;
    float y = (q * f + r) * f + (d - a);
    return c ? (f * .5f) * y + b0 : (y * f) * .5f + b0;
}
/* cResoXBlock::processAudio 0x35c900: controls advance once per64frames. */
static void begin(retro_space *s) {
    cache(s);
    s->remaining = 64;
    s->batch_int[0] = 0;
    if (!SPACE_B(s, 0x134) && get(s, 0x108) < .5f)
        rs_space_clear_state(s);
    if (!SPACE_B(s, 0x2e4)) {
        float x = get(s, 0x2b8);
        x = (x * .15f) / ((x * -.7f + 1) + -.15f);
        SPACE_F(s, 0x648) = x;
        SPACE_F(s, 0x654) = cosf(x * 1.5707964f);
        SPACE_F(s, 0x658) = sinf(x * 1.5707964f) * .2f;
        if (x == 0)
            rs_space_clear_state(s);
    }
    if (get(s, 0x108) <= .5f || get(s, 0x2b8) <= 0) {
        s->batch_int[0] = 1;
        return;
    }
    if (!SPACE_B(s, 0x31c) || !SPACE_B(s, 0x354) || !SPACE_B(s, 0x434) || SPACE_B(s, 0x651)) {
        SPACE_B(s, 0x651) = 0;
        float spread = (get(s, 0x328) == 1 && get(s, 0x408) == 1) ? 1.f : 0.f;
        float det = get(s, 0x2f0) * spread;
        for (unsigned c = 0; c < 2; c++)
            for (unsigned n = 0; n < 12; n++) {
                size_t b = 0x8e0 + c * 0x428 + n * 0x48;
                float note = c ? (float)(23 + n) - det : det + (float)(23 + n);
                float f = (float)((exp2(((double)note + -69.0) / 12.0) * 440.0) / (double)s->rate);
                float len = 1.f / f;
                SPACE_F(s, b + 0x38) = len;
                length(s, b, (int)(len + .5f));
            }
        static const float times[4] = {.0126077095f, .01f, .0077324263f, .0051020407f};
        for (unsigned c = 0; c < 2; c++)
            for (unsigned n = 0; n < 4; n++)
                length(
                    s, 0xc48 + c * 0x428 + n * 0x30,
                    (int)(((spread * .0005215419f) * (c ? .5f : -.5f) + times[n]) * s->rate + .5f));
        SPACE_F(s, 0x644) = -1;
    }
    float decay = get(s, 0x140);
    if (decay != SPACE_F(s, 0x644)) {
        SPACE_F(s, 0x644) = decay;
        float db = (decay - 1) * 48;
        for (unsigned c = 0; c < 2; c++)
            for (unsigned n = 0; n < 12; n++) {
                size_t b = 0x8e0 + c * 0x428 + n * 0x48;
                SPACE_F(s, b + 0x3c) = powf(10, (db / (s->rate / SPACE_F(s, b + 0x38))) / 20);
            }
    }
    float target = get(s, 0x518) * 16384, cur = SPACE_F(s, 0x604);
    if (SPACE_B(s, 0x610)) {
        cur = target;
        SPACE_B(s, 0x610) = 0;
    } else if (cur != target) {
        float delta = (target - cur) * SPACE_F(s, 0x608), sign = delta < 0 ? -1.f : 1.f,
              d = delta * sign;
        cur = d < SPACE_F(s, 0x600) ? target : cur + sign * fminf(SPACE_F(s, 0x60c), d);
    }
    SPACE_F(s, 0x604) = cur;
    s->batch[0] = cur * 6.103515625e-05f;
    for (unsigned c = 0; c < 2; c++) {
        size_t param = c ? 0x218 : 0x178, b = c ? 0x67c : 0x668;
        float f = clamp(SPACE_F(s, param + 0x9c) / s->rate, 0, SPACE_F(s, b + 4));
        if (!SPACE_B(s, param + 0x94) && f != SPACE_F(s, b)) {
            float pole = expf(f * -6.2831855f);
            SPACE_F(s, b) = f;
            SPACE_F(s, b + 8) = 1 - pole;
            SPACE_F(s, b + 12) = (pole + 1) * .5f;
            SPACE_F(s, b + 16) = pole;
        }
    }
    float wet = SPACE_F(s, 0x658), old = SPACE_F(s, 0x660);
    int ramp = wet != old && old >= 0;
    s->batch[1] = ramp ? SPACE_F(s, 0x65c) : SPACE_F(s, 0x654);
    s->batch[2] = ramp ? old : wet;
    s->batch[3] = ramp ? SPACE_F(s, 0x654) - s->batch[1] : 0;
    s->batch[4] = ramp ? wet - old : 0;
    SPACE_F(s, 0x65c) = SPACE_F(s, 0x654);
    SPACE_F(s, 0x660) = wet;
    s->batch_int[1] = get(s, 0x328) != 0;
    s->batch_int[2] = get(s, 0x360) == 1;
    s->batch_int[3] = get(s, 0x408) == 1 && get(s, 0x3d0) == 1;
    s->batch[5] = SPACE_F(s, 0x640);
    s->batch[6] = 0;
    if (s->batch[0] != 0) {
        float amp = (float)(int32_t)SPACE_U(s, 0x5d8) * s->batch[0] * .5f;
        SPACE_F(s, 0x640) = amp;
        s->batch[6] = (amp - s->batch[5]) * .015625f;
    }
    s->batch[7] = clamp(get(s, 0x4e0), 0, 1);
    s->batch[8] = -2;
    float pred = SPACE_F(s, 0x114c) * SPACE_F(s, 0x1138) +
                 ((SPACE_F(s, 0x4dc) * .001f) * s->rate) * SPACE_F(s, 0x1144);
    pred = (pred + .0001f) + -.0001f;
    SPACE_F(s, 0x1138) = pred;
    float next = clamp(pred + 1, 1, s->rate * .1f);
    s->batch[9] = SPACE_F(s, 0x1130);
    s->batch[10] = (next - s->batch[9]) * .015625f;
    s->batch[11] = next;
}
/* Parallel negative-feedback resonators followed by four optional allpasses. */
static float advance(retro_space *s, size_t b, float input, int allpass) {
    float *p = rs_space_pointer(s, b + 32), v = *p;
    if (allpass)
        *p = v * SPACE_F(s, b + 40) + input;
    else
        *p = input;
    p++;
    if (p > (float *)rs_space_pointer(s, b + 24))
        p = rs_space_pointer(s, b + 16);
    rs_space_store_pointer(s, b + 32, p);
    return v;
}
static void sample(retro_space *s, float *l, float *r) {
    unsigned i = 64 - s->remaining;
    float left = *l, right = *r;
    unsigned dual = s->batch_int[2];
    for (unsigned c = 0; c < (dual ? 2u : 1u); c++) {
        size_t b = 0x5e0 + c * 16;
        unsigned idx = (SPACE_U(s, b + 12) + 1) & SPACE_U(s, b + 8);
        SPACE_U(s, b + 12) = idx;
        float *p = rs_space_pointer(s, b);
        p[idx] = dual ? (c ? left * .25f + right * .75f : right * .25f + left * .75f)
                      : (left + right) * .5f;
    }
    float delay = (float)i * s->batch[10] + s->batch[9];
    if (s->batch[0] != 0) {
        float v = sine(SPACE_F(s, 0x628));
        float phase = SPACE_F(s, 0x628) + SPACE_F(s, 0x62c);
        while (phase >= 1)
            phase += -1;
        SPACE_F(s, 0x628) = phase;
        if (s->batch[8] > -2 && signbit(s->batch[8]) != signbit(v)) {
            SPACE_F(s, 0x630) = s->batch[7];
            SPACE_F(s, 0x62c) = SPACE_F(s, 0x634) * s->batch[7];
        }
        s->batch[8] = v;
        float amp = s->batch[6] * (float)i + s->batch[5];
        delay = (v * amp + amp) + delay;
    }
    float in[2] = {cubic(s, 0, delay), 0};
    if (dual)
        in[1] = cubic(s, 1, delay);
    float out[2] = {0, 0}, scale = s->batch_int[3] ? .15f : 1.f;
    in[0] *= scale;
    in[1] *= scale;
    for (unsigned c = 0; c < (s->batch_int[1] ? 2u : 1u); c++) {
        float sum = 0;
        for (unsigned g = 0; g < 3; g++) {
            float v[4];
            for (unsigned n = 0; n < 4; n++) {
                size_t b = 0x8e0 + c * 0x428 + (g * 4 + n) * 0x48,
                       h = 0x760 + c * 0x60 + g * 0x20 + n * 4;
                float tap = *(float *)rs_space_pointer(s, b + 32);
                float hp = SPACE_F(s, h + 16) * SPACE_F(s, 0x678) +
                           (tap - SPACE_F(s, h)) * SPACE_F(s, 0x674);
                SPACE_F(s, h) = tap;
                SPACE_F(s, h + 16) = hp;
                float mix = hp * .4f + tap * .6f;
                float lp = SPACE_F(s, h + 0xd0) * SPACE_F(s, 0x68c) + mix * SPACE_F(s, 0x684);
                SPACE_F(s, h + 0xd0) = lp;
                v[n] = lp * .3f + mix * .7f;
                advance(s, b, in[dual ? c : 0] - SPACE_F(s, b + 0x3c) * v[n], 0);
            }
            sum += ((v[1] + v[0]) + v[2]) + v[3];
        }
        if (s->batch_int[3])
            for (unsigned a = 0; a < 4; a++) {
                float d = advance(s, 0xc48 + c * 0x428 + a * 0x30, sum, 1);
                sum = d - sum;
            }
        out[c] = sum;
    }
    float f = (float)(i + 1) * .015625f, dry = f * s->batch[3] + s->batch[1],
          wet = f * s->batch[4] + s->batch[2];
    *l = wet * out[0] + dry * left;
    *r = wet * out[s->batch_int[1] ? 1 : 0] + dry * right;
}
void retro_space_process(retro_space *s, float *l, float *r, size_t n) {
    if (!s || !s->rate || !l || !r)
        return;
    for (size_t i = 0; i < n; i++) {
        if (!s->remaining)
            begin(s);
        if (!s->batch_int[0])
            sample(s, l + i, r + i);
        if (!--s->remaining && !s->batch_int[0])
            SPACE_F(s, 0x1130) = s->batch[11];
    }
}
