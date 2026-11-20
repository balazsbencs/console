#ifndef RS_NXAC_H
#define RS_NXAC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { RS_NXAC_CLUSTER_FRAMES = 2048 };

typedef struct {
    const uint8_t *bytes;
    size_t size;
    uint32_t frames, cluster_count, data_offset;
    float sample_rate;
} rs_nxac;

/* The caller retains ownership of the immutable encoded bytes. */
bool rs_nxac_open(rs_nxac *out, const void *data, size_t size);

/* Writes 2048 floats per channel to non-overlapping buffers. The last
   cluster can contain padding past the logical count in sample->frames. */
bool rs_nxac_decode_cluster(const rs_nxac *sample, uint32_t cluster, float *left, float *right);

/* Decodes the logical PCM into non-overlapping buffers with capacity floats
   each. The outputs must also be disjoint from the encoded source. */
bool rs_nxac_decode_pcm(const rs_nxac *sample, float *left, float *right, size_t capacity);

#endif
