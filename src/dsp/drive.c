#include "drive.h"
#include "tube_pair.h"
#include "air_pressure.h"
#include "iron_transformer.h"
#include <math.h>
#include <stdlib.h>
struct retro_drive {
    retro_drive_params p;
    float sample_rate;
    rs_tube_pair tube;
    rs_air_pressure air;
    rs_iron_transformer iron;
};
static void setup(retro_drive *s) {
    rs_tube_pair_setup(&s->tube,s->sample_rate,s->p.amount);
    rs_air_pressure_setup(&s->air,s->sample_rate,s->p.amount);
    (void)rs_iron_transformer_setup(&s->iron,s->sample_rate,s->p.amount);
}
retro_drive_params retro_drive_defaults(void) {
    return (retro_drive_params){.amount=.5f,.mix=1,.enable=1};
}
retro_drive *retro_drive_create(void) {
    retro_drive *s=calloc(1,sizeof(*s));
    if (s) s->p=retro_drive_defaults();
    return s;
}
void retro_drive_destroy(retro_drive *s) {
    if (s) {rs_air_pressure_destroy(&s->air);free(s);}
}
int retro_drive_set(retro_drive *s,const retro_drive_params *p) {
    if (!s || !p || !isfinite(p->amount) || p->amount<0 || p->amount>1 ||
        !isfinite(p->character) || p->character<0 || p->character>2 || p->character!=floorf(p->character) ||
        !isfinite(p->mix) || p->mix<0 || p->mix>1 || (p->enable!=0 && p->enable!=1)) return 0;
    int amount_changed=s->p.amount!=p->amount;
    s->p=*p;
    if (s->sample_rate && amount_changed) setup(s);
    return 1;
}
void retro_drive_reset(retro_drive *s) {
    if (!s) return;
    rs_tube_pair_reset(&s->tube);rs_air_pressure_reset(&s->air);rs_iron_transformer_reset(&s->iron);
}
int retro_drive_prepare(retro_drive *s,float rate) {
    if (!s || !isfinite(rate) || rate<1000 || rate>1536000) return 0;
    if (!rs_air_pressure_prepare(&s->air,rate,s->p.amount)) return 0;
    s->sample_rate=rate;setup(s);retro_drive_reset(s);return 1;
}
void retro_drive_process(retro_drive *s,float *left,float *right,size_t frames) {
    if (!s || !left || !right || !s->sample_rate || !s->p.enable) return;
    float dry=1.0f-s->p.mix;
    for (size_t i=0;i<frames;++i) for (unsigned c=0;c<2;++c) {
        float x=c?right[i]:left[i],wet;
        if (s->p.character==0) wet=rs_tube_pair_process(&s->tube,x,c);
        else if (s->p.character==1) wet=rs_air_pressure_process(&s->air,x,c);
        else wet=rs_iron_transformer_process(&s->iron,x,c);
        float out=s->p.mix*wet+dry*x;
        if (c) right[i]=out;else left[i]=out;
    }
}
