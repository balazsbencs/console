#include "drive.h"
#include "tube_pair.h"
#include "air_pressure.h"
#include "iron_transformer.h"
#include "magic.h"
#include <math.h>
#include <stdlib.h>
struct retro_drive {
    retro_drive_params p;
    float sample_rate;
    rs_tube_pair tube;
    rs_air_pressure air;
    rs_iron_transformer iron;
    retro_magic *magic;
};
static void setup(retro_drive *s) {
    rs_tube_pair_setup(&s->tube,s->sample_rate,s->p.amount);
    rs_air_pressure_setup(&s->air,s->sample_rate,s->p.amount);
    (void)rs_iron_transformer_setup(&s->iron,s->sample_rate,s->p.amount);
}
retro_drive_params retro_drive_defaults(void) {
    return (retro_drive_params){.amount=.5f,.mix=1,.enable=1,.low_hz=20,.high_hz=20000,.output_hz=20000};
}
retro_drive *retro_drive_create(void) {
    retro_drive *s=calloc(1,sizeof(*s));
    if (s) {
        s->p=retro_drive_defaults();s->magic=retro_magic_create();
        if(!s->magic) {free(s);return NULL;}
    }
    return s;
}
void retro_drive_destroy(retro_drive *s) {
    if (s) {rs_air_pressure_destroy(&s->air);retro_magic_destroy(s->magic);free(s);}
}
int retro_drive_set(retro_drive *s,const retro_drive_params *p) {
    if (!s || !p || !isfinite(p->amount) || p->amount<0 || p->amount>1 ||
        !isfinite(p->character) || p->character<0 || p->character>2 || p->character!=floorf(p->character) ||
        !isfinite(p->mix) || p->mix<0 || p->mix>1 || (p->enable!=0 && p->enable!=1)) return 0;
    if(p->processing_mode!=0 && p->processing_mode!=1) return 0;
    retro_magic_params mp={.amount=p->amount*100.0f,.character=p->character,
        .low_hz=p->low_hz,.high_hz=p->high_hz,.dist_db=p->dist_db,
        .pass_db=p->pass_db,.mix=p->mix*100.0f,.output_hz=p->output_hz};
    if(!retro_magic_set(s->magic,&mp)) return 0;
    int amount_changed=s->p.amount!=p->amount;
    s->p=*p;
    if (s->sample_rate && amount_changed) setup(s);
    return 1;
}
void retro_drive_reset(retro_drive *s) {
    if (!s) return;
    rs_tube_pair_reset(&s->tube);rs_air_pressure_reset(&s->air);rs_iron_transformer_reset(&s->iron);retro_magic_reset(s->magic);
}
int retro_drive_prepare(retro_drive *s,float rate) {
    if (!s || !isfinite(rate) || rate<1000 || rate>1536000) return 0;
    if (!rs_air_pressure_prepare(&s->air,rate,s->p.amount)) return 0;
    if(!retro_magic_prepare(s->magic,rate)) return 0;
    s->sample_rate=rate;setup(s);retro_drive_reset(s);return 1;
}
void retro_drive_process(retro_drive *s,float *left,float *right,size_t frames) {
    if (!s || !left || !right || !s->sample_rate || !s->p.enable) return;
    if(s->p.processing_mode==1) {retro_magic_process(s->magic,left,right,frames);return;}
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
