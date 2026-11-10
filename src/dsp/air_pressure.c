#include "air_pressure.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
/* Native airPressure state: integrator, gain controls, four first-order filters.
   Retain its single/double precision boundaries and recurrence operation order. */
static float pole(float frequency, float rate) {
    float angle=(float)(((double)frequency*6.2831852)/(double)rate);
    return (float)exp((double)-angle);
}
void rs_air_pressure_setup(rs_air_pressure *s,float rate,float amount) {
    float *v=s->state;
    double a=(double)amount;
    float gain=(float)((exp((a+a)*2.5)*30.0)*.01);
    float compensation=(float)((((1.2+a)+(4.0*a)*a)*.125)/(double)gain);
    float db=((10.0f*amount)+20.0f)*.05000000074505806f;
    v[4]=expf(db*2.3025851249694824f)*gain;
    db=((-14.0f*amount)+((4.0f*amount)*amount))*.05000000074505806f;
    v[5]=expf(db*2.3025851249694824f)*compensation;
    float time=((amount*4050.0f)*.33000001311302185f+1.0f)/rate;
    v[6]=(float)pow(.5,(double)time);
    s->delay=rate*2.2675736545352265e-05f;
    s->inverse_delay=1.0f/s->delay;
    s->sample_rate=rate;
    float ceiling=rate*.49900001287460327f;
    v[7]=pole(fminf(ceiling,20),rate);
    v[8]=(v[7]+1.0f)*.5f;v[9]=-v[8];
    float p=pole(fminf(ceiling,50),rate);
    float b=(1.0f-p)/1.1200000047683716f;
    for (unsigned off=14;off<=28;off+=7) {
        v[off]=p;v[off+1]=b;v[off+2]=b*.11999999731779099f;
    }
}
void rs_air_pressure_reset(rs_air_pressure *s) {
    memset(s->state,0,4*sizeof(float));
    for (unsigned off=7;off<=28;off+=7) memset(s->state+off+3,0,4*sizeof(float));
    for (unsigned c=0;c<2;++c) {
        s->position[c]=0;
        if(s->ring[c]) memset(s->ring[c],0,((size_t)s->mask+1)*sizeof(float));
    }
}
void rs_air_pressure_destroy(rs_air_pressure *s) {
    if (!s) return;
    free(s->ring[0]);free(s->ring[1]);s->ring[0]=s->ring[1]=NULL;
}
int rs_air_pressure_prepare(rs_air_pressure *s,float rate,float amount) {
    if(!s || !isfinite(rate) || rate<1000 || rate>1536000 || !isfinite(amount) || amount<0 || amount>1) return 0;
    uint32_t capacity=4;
    float delay=rate*2.2675736545352265e-05f;
    while(capacity<(uint32_t)delay+4) capacity*=2;
    float *a=calloc(capacity,sizeof(float)),*b=calloc(capacity,sizeof(float));
    if(!a || !b) {free(a);free(b);return 0;}
    free(s->ring[0]);free(s->ring[1]);s->ring[0]=a;s->ring[1]=b;s->mask=capacity-1;
    rs_air_pressure_setup(s,rate,amount);rs_air_pressure_reset(s);return 1;
}
static float filter(float *v,float x,unsigned c) {
    float y=v[0]*v[3+c]+(v[2]*v[5+c]+v[1]*x);
    v[3+c]=y;v[5+c]=x;return y;
}
float rs_air_pressure_process(rs_air_pressure *s,float input,unsigned c) {
    float *v=s->state;
    float x=input*filter(v+14,v[4],c);
    uint32_t pos=(s->position[c]+1)&s->mask;s->position[c]=pos;s->ring[c][pos]=x;
    int delay=(int)s->delay;float t=s->delay-(float)delay;
    uint32_t off=pos-(uint32_t)delay;
    float p1=s->ring[c][(off+1)&s->mask],p0=s->ring[c][off&s->mask];
    float pm1=s->ring[c][(off-1)&s->mask],pm2=s->ring[c][(off-2)&s->mask];
    float a=(p1+p1)+(-5.0f*p0);
    float b=4.0f*pm1+a;b-=pm2;
    float d=pm2-pm1*3.0f;d+=p0*3.0f;d-=p1;
    float interp=(((d*t+b)*t+(pm1-p1))*(.5f*t))+p0;
    float integrated=(float)((double)s->inverse_delay*atan((double)(x-interp)))+v[c];
    v[c]=integrated;
    float y=filter(v+28,v[6],c)*integrated;v[c]=y;
    float out=filter(v+21,v[5],c)*y;
    float hp=filter(v+7,out,c);
    volatile float anti_denormal=hp+9.999999747378752e-05f;
    hp=anti_denormal+-9.999999747378752e-05f;
    v[10+c]=hp;
    return hp;
}
