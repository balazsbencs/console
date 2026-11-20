#include "nxac.h"
#include "nxac_tables.h"

#include <math.h>
#include <string.h>

enum {
    HEADER_BYTES = 0x128,
    ENTRY_BYTES = 16,
    BLOCK_FRAMES = 64,
    BLOCKS_PER_CLUSTER = RS_NXAC_CLUSTER_FRAMES / BLOCK_FRAMES
};

static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static float read_float(const uint8_t *p) {
    _Static_assert(sizeof(float) == sizeof(uint32_t), "NXAC requires 32-bit float");
    uint32_t bits = read_u32(p);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static unsigned group_bytes(unsigned format) {
    return format ? 2 + (format - 1) % 3 : 0;
}

static unsigned component_bytes(unsigned format) {
    return format ? 2 + (BLOCK_FRAMES / 4) * group_bytes(format) : 0;
}

bool rs_nxac_open(rs_nxac *out, const void *data, size_t size) {
    if (!out || !data || size < HEADER_BYTES)
        return false;

    const uint8_t *bytes = data;
    if (memcmp(bytes, "NXAC v1.1\n", 10) != 0 || read_u32(bytes + 0x100) != 0x3a5920b3u ||
        read_u32(bytes + 0x104) != 2 || read_u32(bytes + 0x10c) != RS_NXAC_CLUSTER_FRAMES ||
        read_u32(bytes + 0x110) != BLOCK_FRAMES || read_u32(bytes + 0x118) != HEADER_BYTES)
        return false;

    uint32_t frames = read_u32(bytes + 0x108);
    uint32_t clusters = frames / RS_NXAC_CLUSTER_FRAMES + (frames % RS_NXAC_CLUSTER_FRAMES != 0);
    float rate = read_float(bytes + 0x114);
    uint32_t table_size = read_u32(bytes + 0x11c);
    uint32_t data_offset = read_u32(bytes + 0x120);
    uint32_t data_size = read_u32(bytes + 0x124);
    if (!frames || !isfinite(rate) || rate <= 0 || table_size != clusters * ENTRY_BYTES ||
        data_offset != HEADER_BYTES + table_size || data_offset > size || size - data_offset != data_size)
        return false;

    uint32_t next_offset = 0;
    for (uint32_t i = 0; i < clusters; ++i) {
        const uint8_t *entry = bytes + HEADER_BYTES + i * ENTRY_BYTES;
        uint32_t packed_formats = read_u32(entry + 12);
        unsigned mid = entry[12], side = entry[13];
        float rms = read_float(entry + 4);
        if (read_float(entry) != rate || !isfinite(rms) || rms < 0 ||
            packed_formats > 0xffffu || mid > 9 || side > 9 ||
            read_u32(entry + 8) != next_offset)
            return false;

        uint32_t cluster_bytes = BLOCKS_PER_CLUSTER * (component_bytes(mid) + component_bytes(side));
        if (cluster_bytes > data_size - next_offset)
            return false;
        next_offset += cluster_bytes;
    }
    if (next_offset != data_size)
        return false;

    *out = (rs_nxac){ .bytes = bytes, .size = size, .frames = frames,
        .cluster_count = clusters, .data_offset = data_offset, .sample_rate = rate };
    return true;
}

typedef struct {
    float gain, value, velocity;
    unsigned width, order;
} component;

static component init_component(unsigned format, const uint8_t *indices) {
    if (!format)
        return (component){0};
    unsigned width = group_bytes(format);
    return (component){
        .gain = rs_nxac_gain(indices[0]) / (float)(1u << (2 * width)),
        .value = rs_nxac_delta(indices[1]),
        .width = width,
        .order = (format - 1) / 3,
    };
}

/* Each group encodes four unsigned codes, most significant bits first. */
static void decode_group(component *state, const uint8_t *bytes, float out[4]) {
    if (!state->width) {
        memset(out, 0, 4 * sizeof(*out));
        return;
    }
    uint32_t packed = 0;
    for (unsigned i = 0; i < state->width; ++i)
        packed = (packed << 8) | bytes[i];

    unsigned bits = 2 * state->width;
    unsigned mask = (1u << bits) - 1;
    float center = mask * 0.5f;
    for (unsigned i = 0; i < 4; ++i) {
        unsigned code = (packed >> ((3 - i) * bits)) & mask;
        float residual = ((float)code - center) * state->gain;
        switch (state->order) {
        case 0:
            out[i] = state->value + residual;
            break;
        case 1:
            state->value += residual;
            out[i] = state->value;
            break;
        default:
            state->velocity += residual;
            state->value += state->velocity;
            out[i] = state->value;
            break;
        }
    }
}

static void decode_cluster(const rs_nxac *sample, uint32_t cluster, float *left, float *right,
                           unsigned frames) {
    const uint8_t *entry = sample->bytes + HEADER_BYTES + cluster * ENTRY_BYTES;
    const uint8_t *input = sample->bytes + (size_t)sample->data_offset + read_u32(entry + 8);
    unsigned mid_format = entry[12], side_format = entry[13];

    while (frames) {
        component mid = init_component(mid_format, input);
        input += mid_format ? 2 : 0;
        component side = init_component(side_format, input);
        input += side_format ? 2 : 0;

        for (unsigned frame = 0; frame < BLOCK_FRAMES && frames; frame += 4) {
            float m[4], s[4];
            decode_group(&mid, input, m);
            input += mid.width;
            decode_group(&side, input, s);
            input += side.width;
            for (unsigned i = 0; i < 4 && frames; ++i, --frames) {
                *left++ = m[i] + s[i];
                *right++ = m[i] - s[i];
            }
        }
    }
}

bool rs_nxac_decode_cluster(const rs_nxac *sample, uint32_t cluster, float *left, float *right) {
    if (!sample || !sample->bytes || cluster >= sample->cluster_count || !left || !right || left == right)
        return false;
    decode_cluster(sample, cluster, left, right, RS_NXAC_CLUSTER_FRAMES);
    return true;
}

bool rs_nxac_decode_pcm(const rs_nxac *sample, float *left, float *right, size_t capacity) {
    if (!sample || !sample->bytes || !left || !right || left == right || capacity < sample->frames)
        return false;
    size_t offset = 0;
    for (uint32_t cluster = 0; cluster < sample->cluster_count; ++cluster) {
        unsigned remaining = sample->frames - offset;
        unsigned count = remaining < RS_NXAC_CLUSTER_FRAMES ? remaining : RS_NXAC_CLUSTER_FRAMES;
        decode_cluster(sample, cluster, left + offset, right + offset, count);
        offset += count;
    }
    return true;
}
