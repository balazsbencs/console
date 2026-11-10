#ifndef RS_CLASSIC_SPLIT_H
#define RS_CLASSIC_SPLIT_H
/* Native object fields +0x48..+0x5c, in the same order. */
typedef struct {
    float low_coefficient, middle_coefficient;
    float history[2][2]; /* channel, then low/middle */
} rs_classic_split_state;
typedef struct {
    float low[2][4], middle[2][4], high[2][4];
} rs_classic_split_bands;
void rs_classic_split_sample(rs_classic_split_state *s, float left, float right,
                             float low[2], float middle[2], float high[2]);
void rs_classic_split(rs_classic_split_state *s, const float left[4],
                      const float right[4], rs_classic_split_bands *out);
/* Original mono helper touches channel zero histories only. */
void rs_classic_split_mono(rs_classic_split_state *s, const float input[4],
                           float low[4], float middle[4], float high[4]);
#endif
