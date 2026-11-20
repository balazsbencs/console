#include "nxac.h"
#include "noise_interp.h"
#include "noise_player.h"

#include <assert.h>
#include <string.h>

enum { TABLE = 0x128, DATA = TABLE + 16, BLOCK_BYTES = 34 + 50 };
static uint8_t file[DATA + BLOCK_BYTES * 32];

static void put_u32(size_t offset, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        file[offset + i] = (uint8_t)(value >> (8 * i));
}

static void make_fixture(void) {
    memcpy(file, "NXAC v1.1\n", 10);
    put_u32(0x100, 0x3a5920b3);
    put_u32(0x104, 2);
    put_u32(0x108, 1999);
    put_u32(0x10c, RS_NXAC_CLUSTER_FRAMES);
    put_u32(0x110, 64);
    put_u32(0x114, 0x472c4400); /* 44100 Hz */
    put_u32(0x118, TABLE);
    put_u32(0x11c, 16);
    put_u32(0x120, DATA);
    put_u32(0x124, sizeof(file) - DATA);
    put_u32(TABLE, 0x472c4400);
    put_u32(TABLE + 12, 4 | (8u << 8));

    for (size_t i = DATA; i < sizeof(file); i += BLOCK_BYTES) {
        file[i] = file[i + 2] = 30; /* unit gain before residual scaling */
        for (size_t j = i + 4; j < i + BLOCK_BYTES; j += 5) {
            file[j] = 0x89;
            file[j + 1] = 0x67;
            file[j + 2] = 0x82;
            file[j + 3] = 0x17;
            file[j + 4] = 0x9f; /* codes 32, 33, 30, 31 */
        }
    }
}

static void check_decoder(void) {
    make_fixture();
    rs_nxac sample = {0};
    assert(rs_nxac_open(&sample, file, sizeof(file)));
    assert(sample.frames == 1999 && sample.cluster_count == 1 && sample.sample_rate == 44100);
    float left[RS_NXAC_CLUSTER_FRAMES], right[RS_NXAC_CLUSTER_FRAMES];
    assert(rs_nxac_decode_cluster(&sample, 0, left, right));
    assert(!rs_nxac_decode_cluster(&sample, 1, left, right));
    assert(!rs_nxac_decode_cluster(&sample, 0, left, left));

    const float residuals[] = {0.5f, 1.5f, -1.5f, -0.5f};
    for (size_t block = 0; block < RS_NXAC_CLUSTER_FRAMES; block += 64) {
        float mid = 0, side = 0, velocity = 0;
        for (size_t i = 0; i < 64; ++i) {
            mid += residuals[i % 4] / 16;
            velocity += residuals[i % 4] / 64;
            side += velocity;
            assert(left[block + i] == mid + side);
            assert(right[block + i] == mid - side);
        }
    }

    float pcm_left[2000], pcm_right[2000];
    pcm_left[1999] = pcm_right[1999] = 42;
    assert(!rs_nxac_decode_pcm(&sample, pcm_left, pcm_right, 1998));
    assert(rs_nxac_decode_pcm(&sample, pcm_left, pcm_right, 1999));
    assert(memcmp(pcm_left, left, 1999 * sizeof(float)) == 0);
    assert(memcmp(pcm_right, right, 1999 * sizeof(float)) == 0);
    assert(pcm_left[1999] == 42 && pcm_right[1999] == 42);

    assert(!rs_nxac_open(&sample, file, sizeof(file) - 1));
    file[TABLE + 12] = 10;
    assert(!rs_nxac_open(&sample, file, sizeof(file)));
    file[TABLE + 12] = 4;
    put_u32(TABLE + 8, 1);
    assert(!rs_nxac_open(&sample, file, sizeof(file)));
    put_u32(TABLE + 8, 0);
    put_u32(0x11c, 32);
    assert(!rs_nxac_open(&sample, file, sizeof(file)));
}

static void check_player(void) {
    const float left[] = {0.38f, -0.4f, 0.8f, -0.17f, 0.3f};
    const float right[] = {-0.62f, 0.38f, -0.83f, 0.1f, -0.2f};
    float out_left[31], out_right[31];
    rs_noise_player player = {0};
    assert(rs_noise_player_prepare(&player, left, right, 5, 44100, 44100));
    rs_noise_player_process(&player, out_left, out_right, 31);
    for (size_t i = 0; i < 31; ++i) {
        assert(out_left[i] == left[i % 5]);
        assert(out_right[i] == right[i % 5]);
    }

    assert(rs_noise_player_prepare(&player, left, right, 5, 44100, 88200));
    rs_noise_player_process(&player, out_left, out_right, 31);
    for (size_t i = 0; i < 31; ++i) {
        size_t center = (i / 2) % 5;
        float fraction = i % 2 ? 0.5f : 0;
        size_t previous = (center + 4) % 5, next = (center + 1) % 5, following = (center + 2) % 5;
        assert(out_left[i] == rs_noise_interpolate(left[previous], left[center], left[next], left[following], fraction));
        assert(out_right[i] == rs_noise_interpolate(right[previous], right[center], right[next], right[following], fraction));
    }

    float split_left[31], split_right[31];
    rs_noise_player_reset(&player);
    rs_noise_player_process(&player, split_left, split_right, 7);
    rs_noise_player_process(&player, split_left + 7, split_right + 7, 24);
    assert(memcmp(split_left, out_left, sizeof(out_left)) == 0);
    assert(memcmp(split_right, out_right, sizeof(out_right)) == 0);
}

int main(void) {
    check_decoder();
    check_player();
    return 0;
}
