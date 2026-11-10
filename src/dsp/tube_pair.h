#ifndef RETRO_TUBE_PAIR_H
#define RETRO_TUBE_PAIR_H
/* Isolated original TubePair numerical state: five stereo first-order
   filters (seven floats each), then two gains. setup preserves histories.
   sample_rate must be finite and positive, amount finite [0,1]. */
typedef struct { float state[37]; } rs_tube_pair;
void rs_tube_pair_setup(rs_tube_pair *s, float sample_rate, float amount);
/* Clears only filter histories; preserves coefficients and gains. */
void rs_tube_pair_reset(rs_tube_pair *s);
/* Channel is zero or one. State access on a single instance must serialize. */
float rs_tube_pair_process(rs_tube_pair *s, float sample, unsigned channel);
#endif
