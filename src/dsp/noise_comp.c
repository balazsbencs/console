#include "noise_comp.h"
#include <math.h>
#include <string.h>
#define F(o) (s->native.f[((o) - 0x338) / 4])
#define U(o) (s->native.u[((o) - 0x338) / 4])
static const float epsilon = 1.0000000195414814e-25f;
rs_noise_comp_params rs_noise_comp_defaults(void) {
    return (rs_noise_comp_params){1, 0, .5f, 0, .2f, 0, 0, 1};
}
int rs_noise_comp_set(rs_noise_comp *s, const rs_noise_comp_params *p) {
    if (!s || !p)
        return 0;
    float v[8];
    memcpy(v, p, sizeof(v));
    for (unsigned i = 0; i < 8; i++)
        if (!isfinite(v[i]) || v[i] < 0 || v[i] > 1)
            return 0;
    if (p->enable != 0 && p->enable != 1)
        return 0;
    s->p = *p;
    return 1;
}
void rs_noise_comp_reset(rs_noise_comp *s) {
    if (!s)
        return;
    memset(&s->native, 0, sizeof(s->native));
    F(0x33c) = F(0x340) = -1;
    F(0x350) = F(0x358) = epsilon;
    if (s->rate) {
        F(0x354) = (float)exp((double)(-1.f / (s->rate * .004999999888241291f)));
        float w = fminf(80.f / s->rate, .49000000953674316f) * 6.2831854820251465f;
        float alpha = (sinf(w) * .5f) / .7070000171661377f, c = cosf(w),
              scale = 1.f / (alpha + 1.f);
        float b1 = -1.f - c, b0 = b1 * -.5f;
        F(0x37c) = b0 * scale;
        F(0x380) = b1 * scale;
        F(0x384) = b0 * scale;
        F(0x388) = (c * -2.f) * scale;
        F(0x38c) = (1.f - alpha) * scale;
        F(0x390) = alpha;
        F(0x394) = c;
    }
    s->remaining = 0;
}
int rs_noise_comp_prepare(rs_noise_comp *s, float rate) {
    if (!s || !isfinite(rate) || rate < 1000 || rate > 1536000)
        return 0;
    s->rate = rate;
    rs_noise_comp_reset(s);
    return 1;
}
static void begin(rs_noise_comp *s) {
    s->active = s->p;
    s->remaining = 64;
    if (s->active.enable != 1) {
        F(0x350) = F(0x358) = epsilon;
        return;
    }
    float a = s->active.attack, r = s->active.release;
    if (a != F(0x33c)) {
        F(0x33c) = a;
        float t = a * .6847460865974426f + .022360680624842644f;
        F(0x348) = (float)exp((double)(-1.f / ((fabsf(t) * t) * s->rate)));
    }
    if (r != F(0x340)) {
        F(0x340) = r;
        float t = r * 1.314213514328003f + .10000000149011612f;
        F(0x34c) = (float)exp((double)(-1.f / ((fabsf(t) * t) * s->rate)));
    }
    s->threshold_db = s->active.threshold * -48.f;
    s->ratio_coefficient = 1.f / expf(s->active.ratio * 2.079441547393799f);
    s->gain_offset = 0;
    if (s->active.gain_comp > 0) {
        float offset = .25f * s->threshold_db;
        s->ratio_coefficient += -1.f;
        s->gain_offset = offset * s->ratio_coefficient;
    } else
        s->ratio_coefficient += -1.f;
}
static float highpass(rs_noise_comp *s, float x, unsigned c) {
    unsigned h = 0x35c + c * 16;
    float y = ((F(h + 4) * F(0x384) + (F(h) * F(0x380) + F(0x37c) * x)) - F(h + 8) * F(0x388)) -
              F(h + 12) * F(0x38c);
    volatile float rounded = y + 9.999999747378752e-05f;
    y = rounded + -9.999999747378752e-05f;
    F(h + 4) = F(h);
    F(h) = x;
    F(h + 12) = F(h + 8);
    F(h + 8) = y;
    return y;
}
void rs_noise_comp_process(rs_noise_comp *s, float *l, float *r, const float *sl, const float *sr,
                           size_t n) {
    if (!s || !s->rate || !l || !r)
        return;
    for (size_t i = 0; i < n; i++) {
        if (!s->remaining)
            begin(s);
        float a = sl ? sl[i] : 0, b = sr ? sr[i] : 0;
        if (s->active.highpass != 0) {
            a = highpass(s, a, 0);
            b = highpass(s, b, 1);
        }
        if (s->active.enable == 1) {
            float amplitude;
            if (s->active.averaging != 0) {
                float power = a * a + b * b;
                power += epsilon;
                F(0x358) = (F(0x358) - power) * F(0x354) + power;
                amplitude = sqrtf(F(0x358)) + epsilon;
            } else
                amplitude = fmaxf(fabsf(a), fabsf(b)) + epsilon;
            float db = (float)(log10((double)amplitude) * 20.0);
            float target = fmaxf(0, db - s->threshold_db) + epsilon;
            float pole = target > F(0x350) ? F(0x348) : F(0x34c);
            float smooth = (F(0x350) - target) * pole + target;
            float reduction = (smooth + -epsilon) * s->ratio_coefficient;
            float gain = (float)pow(10.0, (double)(s->gain_offset + reduction) / 20.0);
            F(0x350) = smooth;
            l[i] *= gain;
            r[i] *= gain;
        }
        if (!--s->remaining && s->active.enable == 1)
            U(0x344) = (U(0x344) + 1) & 15;
    }
}
