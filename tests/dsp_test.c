#include "bitcolor.h"
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
    puts("DSP block partitioning, reset, in-place rendering and invalid controls passed");
    return 0;
}
