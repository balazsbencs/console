#include "bitcolor.h"
#include "classic.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void check_partition(retro_digital_params p) {
    retro_digital a = {0}, b = {0};
    assert(retro_digital_set(&a, &p) && retro_digital_set(&b, &p));
    assert(retro_digital_prepare(&a, 44100) && retro_digital_prepare(&b, 44100));
    enum { N = 8192 };
    float left[N], right[N], out[2][N], split[2][N];
    for (unsigned i = 0; i < N; ++i) {
        left[i] = (float)sin(i * 0.03); right[i] = (float)cos(i * 0.073);
    }
    retro_digital_process(&a, left, right, out[0], out[1], N);
    const unsigned lengths[] = {1, 7, 63, 64, 65, 127, 511};
    unsigned offset = 0, block = 0;
    while (offset < N) {
        unsigned size = lengths[block++ % 7]; if (size > N - offset) size = N - offset;
        retro_digital_process(&b, left + offset, right + offset, split[0] + offset, split[1] + offset, size);
        offset += size;
    }
    assert(memcmp(out, split, sizeof(out)) == 0);
    retro_digital_reset(&b);
    memcpy(split[0], left, sizeof(left)); memcpy(split[1], right, sizeof(right));
    retro_digital_process(&b, split[0], split[1], split[0], split[1], N);
    assert(memcmp(out, split, sizeof(out)) == 0);
    retro_digital_params before = b.params; p.bits = NAN;
    assert(!retro_digital_set(&b, &p) && memcmp(&b.params, &before, sizeof(before)) == 0);
    assert(!retro_digital_prepare(&b, INFINITY));
}

static void check_classic_contract(unsigned character,float rate,int crossed) {
    enum {N=4096};
    retro_classic *a=retro_classic_create(),*b=retro_classic_create();assert(a && b);
    retro_classic_params p=retro_classic_defaults();
    p.amount=73;p.character=(float)character;p.mix=64;p.dist_db=-3;p.pass_db=1;p.reference_db=-15;
    p.low_hz=crossed?9000:220;p.high_hz=crossed?700:6500;p.output_hz=4700;
    assert(retro_classic_set(a,&p) && retro_classic_set(b,&p));
    assert(retro_classic_prepare(a,rate) && retro_classic_prepare(b,rate));
    assert(!retro_classic_set(a,NULL) && !retro_classic_set(NULL,&p));
    const float invalid_rates[]={NAN,INFINITY,999,1536001};
    for(unsigned i=0;i<sizeof(invalid_rates)/sizeof(*invalid_rates);++i)
        assert(!retro_classic_prepare(a,invalid_rates[i]));
    float input[2][N],whole[2][N],split[2][N];
    for(unsigned i=0;i<N;++i) {
        input[0][i]=.6f*(float)sin(i*.071);input[1][i]=.4f*(float)cos(i*.093);
    }
    memcpy(whole,input,sizeof(whole));memcpy(split,input,sizeof(split));
    retro_classic_process(a,whole[0],whole[1],N);
    const unsigned lengths[]={1,7,63,64,65,127,511};
    unsigned offset=0,block=0;
    while(offset<N) {
        /* Reject every public parameter family with histories populated. */
        if(offset) for(unsigned field=0;field<9;++field) {
            retro_classic_params invalid=p;
            if(field==0)invalid.amount=NAN;
            else if(field==1)invalid.character=.5f;
            else if(field==2)invalid.low_hz=19;
            else if(field==3)invalid.high_hz=20001;
            else if(field==4)invalid.dist_db=101;
            else if(field==5)invalid.pass_db=-101;
            else if(field==6)invalid.mix=101;
            else if(field==7)invalid.output_hz=INFINITY;
            else invalid.reference_db=NAN;
            assert(!retro_classic_set(b,&invalid));
        }
        unsigned size=lengths[block++%7];if(size>N-offset)size=N-offset;
        retro_classic_process(b,split[0]+offset,split[1]+offset,size);offset+=size;
    }
    assert(memcmp(whole,split,sizeof(whole))==0);
    for(unsigned c=0;c<2;++c)for(unsigned i=0;i<N;++i)assert(isfinite(whole[c][i]));
    retro_classic_reset(b);memcpy(split,input,sizeof(split));
    retro_classic_process(b,split[0],split[1],N);
    assert(memcmp(whole,split,sizeof(whole))==0);
    p.mix=0;assert(retro_classic_set(b,&p));retro_classic_reset(b);memcpy(split,input,sizeof(split));
    retro_classic_process(b,split[0],split[1],N);
    assert(memcmp(input,split,sizeof(input))==0);
    retro_classic_destroy(a);retro_classic_destroy(b);
}

int main(void) {
    retro_digital_params p = retro_digital_defaults();
    p.smooth = 0.67f; p.feedback = 0.81f; p.rate = 127.0f;
    check_partition(p);
    p.focus = 0; p.low = 700; p.high = 2300;
    check_partition(p);
    p.normalized = 1; p.bits = 0.23f; p.rate = 0.71f;
    p.mod_rate = 0.87f; p.mod_depth = 0.94f;
    check_partition(p);
    p.focus = 1;
    check_partition(p);
    const float classic_rates[]={1000,44100,96000,384000};
    for(unsigned r=0;r<sizeof(classic_rates)/sizeof(*classic_rates);++r)
        for(unsigned character=0;character<3;++character)
            for(int crossed=0;crossed<2;++crossed)check_classic_contract(character,classic_rates[r],crossed);
    puts("DSP block partitioning, reset, in-place rendering and invalid controls passed");
    return 0;
}
