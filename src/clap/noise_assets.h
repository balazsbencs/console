#ifndef RS_NOISE_ASSETS_H
#define RS_NOISE_ASSETS_H

#include "noise_player.h"

enum { RS_NOISE_TYPES = 16, RS_NOISE_WHITE = 11 };

typedef struct rs_noise_assets rs_noise_assets;
const char *rs_noise_type_name(unsigned type);
const rs_noise_assets *rs_noise_assets_acquire(void);
void rs_noise_assets_release(const rs_noise_assets *assets);
bool rs_noise_assets_prepare(const rs_noise_assets *assets, unsigned type, double rate,
                             rs_noise_player *player);

#endif
