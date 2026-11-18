#include "space_internal.h"
#include <math.h>
#include <stdlib.h>
static float rounded(float x) {volatile float y=x+9.999999747378752e-05f;return y+-9.999999747378752e-05f;}
static void free_buffer(retro_space *s,size_t o) {free(rs_space_pointer(s,o));rs_space_store_pointer(s,o,NULL);}
void rs_space_free_state(retro_space *s) {
    for(unsigned c=0;c<2;++c) {
        for(unsigned i=0;i<12;++i) free_buffer(s,0x8f0+c*0x428+i*0x48);
        for(unsigned i=0;i<4;++i) free_buffer(s,0xc58+c*0x428+i*0x30);
        free_buffer(s,0x5e0+c*0x10);
    }
}
static int delay(retro_space *s,size_t o,unsigned capacity,unsigned length,float *existing) {
    float *p=existing?existing:calloc(capacity,sizeof(float));if(!p) return 0;
    if(existing) memset(p,0,capacity*sizeof(float));
    if(length<1) length=1;
    if(length>capacity) length=capacity;
    SPACE_U(s,o+8)=capacity;SPACE_U(s,o+12)=length;
    rs_space_store_pointer(s,o+16,p);rs_space_store_pointer(s,o+24,p+length-1);rs_space_store_pointer(s,o+32,p);
    return 1;
}
static void filter(retro_space *s,size_t o,float frequency) {
    frequency=fminf(fmaxf(frequency,0),SPACE_F(s,o+4));
    SPACE_F(s,o)=frequency;
    float pole=expf(frequency*-6.2831854820251465f);
    SPACE_F(s,o+16)=pole;SPACE_F(s,o+8)=1.0f-pole;SPACE_F(s,o+12)=(pole+1.0f)*.5f;
}
static void *previous_pointer(const rs_space_native *n,size_t o) {void *p;memcpy(&p,n->bytes+o,sizeof(p));return p;}
static int initialize(retro_space *s,float rate,int reuse) {
    rs_space_native previous=s->native;
    if(!reuse) rs_space_free_state(s);
    memset(s->native.bytes+0x5a0,0,0x1160-0x5a0);
    SPACE_U(s,0xf4)=2;SPACE_F(s,0xf8)=rate;SPACE_F(s,0x5c0)=1.0f/rate;
    SPACE_F(s,0x608)=1;SPACE_F(s,0x60c)=1;SPACE_B(s,0x610)=1;
    SPACE_F(s,0x620)=-.5f;SPACE_F(s,0x628)=0;SPACE_F(s,0x62c)=.00045351474545896053f;
    SPACE_F(s,0x630)=20;SPACE_F(s,0x634)=2.2675736545352265e-05f;
    SPACE_F(s,0x644)=-1;SPACE_F(s,0x648)=-1;SPACE_F(s,0x64c)=-1;
    SPACE_F(s,0x65c)=-1;SPACE_F(s,0x660)=-1;SPACE_U(s,0x664)=UINT32_MAX;
    SPACE_F(s,0x668)=-1;SPACE_F(s,0x66c)=.5f;SPACE_F(s,0x670)=1;SPACE_F(s,0x674)=1;
    SPACE_F(s,0x67c)=-1;SPACE_F(s,0x680)=.5f;SPACE_F(s,0x684)=1;SPACE_F(s,0x688)=1;
    SPACE_F(s,0x1130)=1;SPACE_F(s,0x113c)=-1;SPACE_F(s,0x1140)=.5f;
    SPACE_F(s,0x1144)=1;SPACE_F(s,0x1148)=1;SPACE_U(s,0x1150)=UINT32_MAX;
    for(unsigned i=0;i<12;++i) {
        double hz=exp2(((double)(i+22)+-69.0)/12.0)*440.0;
        float normalized=(float)(hz/(double)rate);
        unsigned capacity=(unsigned)(1.0f/normalized)+1;
        for(unsigned c=0;c<2;++c) {
            size_t o=0x8e0+c*0x428+i*0x48;
            if(!delay(s,o,capacity,1,reuse?previous_pointer(&previous,o+16):NULL)) goto fail;
        }
    }
    const float seconds[]={.012868480756878853f,.01026077102869749f,.007993197068572044f,.0053628114983439445f};
    for(unsigned i=0;i<4;++i) {
        unsigned length=(unsigned)(rate*seconds[i]+.5f);if(!length) length=1;
        for(unsigned c=0;c<2;++c) {
            size_t o=0xc48+c*0x428+i*0x30;
            if(!delay(s,o,length,length,reuse?previous_pointer(&previous,o+16):NULL)) goto fail;
            SPACE_F(s,o+0x28)=.5f;
        }
    }
    SPACE_B(s,0x651)=1;
    SPACE_F(s,0x5b0)=1.0f-expf((20.0f/rate)*-6.2831854820251465f);
    unsigned wobble=(unsigned)(rate*.009999999776482582f)*2+1;
    SPACE_U(s,0x5d8)=wobble;
    float required=(float)wobble+(rate*.10000000149011612f+.5f);
    required+=.5f;SPACE_U(s,0x5dc)=(unsigned)required;
    unsigned capacity=2;while(capacity<SPACE_U(s,0x5dc)) capacity*=2;
    for(unsigned c=0;c<2;++c) {
        float *p=reuse?previous_pointer(&previous,0x5e0+c*0x10):calloc(capacity,sizeof(float));if(!p) goto fail;
        if(reuse) memset(p,0,capacity*sizeof(float));
        rs_space_store_pointer(s,0x5e0+c*0x10,p);SPACE_U(s,0x5e8+c*0x10)=capacity-1;
    }
    float cutoff=((rate*.004999999888241291f)*.015625f)*16384.0f;
    SPACE_F(s,0x60c)=cutoff;SPACE_F(s,0x608)=1.0f-expf((cutoff*-3.1415927410125732f)/.0010000000474974513f);
    SPACE_U(s,0x600)=0x3c8637bd;SPACE_B(s,0x610)=1;
    float predelay=(s->p.pre_delay_ms*.0010000000474974513f)*rate;
    SPACE_F(s,0x1134)=predelay;SPACE_F(s,0x1138)=predelay;
    filter(s,0x113c,64.0f/rate);
    float smoothed=predelay*SPACE_F(s,0x1144)+predelay*SPACE_F(s,0x114c);
    SPACE_F(s,0x1138)=rounded(smoothed);
    SPACE_F(s,0x1130)=fmaxf(1,fminf(SPACE_F(s,0x1138)+1.0f,rate*.1f));
    SPACE_F(s,0x634)=1.0f/rate;SPACE_F(s,0x628)=.75f;SPACE_F(s,0x630)=.1f;
    SPACE_F(s,0x62c)=(1.0f/rate)*.1f;
    filter(s,0x668,s->p.dampen_hz/rate);filter(s,0x67c,s->p.soften_hz/rate);
    return 1;
fail:
    rs_space_free_state(s);s->rate=0;return 0;
}
int rs_space_prepare_state(retro_space *s,float rate) {return initialize(s,rate,0);}
void rs_space_rewind_state(retro_space *s) {(void)initialize(s,s->rate,1);}
void rs_space_clear_state(retro_space *s) {
    for(unsigned c=0;c<2;++c) {
        for(unsigned i=0;i<12;++i) {
            size_t o=0x8e0+c*0x428+i*0x48;
            float *p=rs_space_pointer(s,o+16);
            if(p) memset(p,0,SPACE_U(s,o+12)*sizeof(float));
            memset(s->native.bytes+o+0x28,0,16);
        }
        for(unsigned i=0;i<4;++i) {
            size_t o=0xc48+c*0x428+i*0x30;float *p=rs_space_pointer(s,o+16);
            if(p) memset(p,0,SPACE_U(s,o+12)*sizeof(float));
        }
        float *p=rs_space_pointer(s,0x5e0+c*0x10);
        if(p) memset(p,0,(SPACE_U(s,0x5e8+c*0x10)+1)*sizeof(float));
    }
}
