#include "gain.h"
#include <math.h>
int rs_gain_set(rs_gain *s, float linear_gain, float db) {
    if (!s || !isfinite(linear_gain) || linear_gain < -100 || linear_gain > 100 ||
        !isfinite(db) || db < -100 || db > 100) return 0;
    s->linear_gain=linear_gain;s->db=db;
    s->target=expf((db*.05000000074505806f)*2.3025851249694824f)*linear_gain;
    return 1;
}
void rs_gain_reset(rs_gain *s) {
    if (!s) return;
    s->current=s->target;s->remaining=0;s->lane=0;s->smoothing=0;
}
int rs_gain_prepare(rs_gain *s, float sample_rate) {
    if (!s || !isfinite(sample_rate) || sample_rate<1000 || sample_rate>1536000) return 0;
    s->coefficient=1.0f-expf(fminf(40.0f/sample_rate,.49998998641967773f)*-6.2831854820251465f);
    s->block_coefficient=1.0f-expf(fminf((64.0f*10.0f)/sample_rate,.49998998641967773f)*-6.2831854820251465f);
    rs_gain_reset(s);return 1;
}
void rs_gain_process(rs_gain *s, float *left, float *right, size_t frames) {
    if (!s || !left || !right) return;
    for (size_t i=0;i<frames;++i) {
        if (!s->remaining) {
            float target=s->target;
            if (fabsf(target)<9.999999717180685e-10f) target=0;
            if (fabsf(target-1.0f)<9.999999717180685e-10f) target=1;
            s->block_target=target;
            s->smoothing=fabsf(s->current-target)>=9.999999747378752e-05f;
            if (!s->smoothing) s->current=target;
            s->remaining=64;s->lane=0;
        }
        float gain=s->current;
        if (s->smoothing) {
            if (!s->lane) {
                s->group_start=s->current;
                s->group_delta=(s->block_target-s->current)*s->coefficient;
            }
            gain=s->group_start+s->group_delta*((float)s->lane*.25f);
        }
        if (!s->smoothing && gain==0) left[i]=right[i]=0;
        else if (s->smoothing || gain!=1) {left[i]*=gain;if (right!=left) right[i]*=gain;}
        if (s->smoothing && ++s->lane==4) {
            s->current=s->group_start+s->group_delta;s->lane=0;
        }
        --s->remaining;
    }
}
