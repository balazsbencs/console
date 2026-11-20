#include "noise_assets.h"
#include "nxac.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct {
    const char *name;
    unsigned id;
} textures[RS_NOISE_TYPES] = {
    {"Vinyl 1", 4}, {"Vinyl 2", 12}, {"7.5 inch", 5}, {"15 inch", 6},
    {"Tube", 0}, {"DC", 2}, {"Muff", 3}, {"Radio", 13},
    {"VHS", 8}, {"Cassette", 9}, {"8-bit", 10}, {"White", 1},
    {"Pink", 11}, {"Studio 1", 7}, {"Studio 2", 14}, {"Apollo", 15},
};

typedef struct {
    float *pcm;
    uint32_t frames;
    float rate;
} texture;

struct rs_noise_assets {
    texture clips[RS_NOISE_TYPES];
};

static rs_noise_assets bank;
static size_t references;
static pthread_mutex_t bank_lock = PTHREAD_MUTEX_INITIALIZER;

const char *rs_noise_type_name(unsigned type) {
    return type < RS_NOISE_TYPES ? textures[type].name : NULL;
}

static FILE *open_texture(unsigned id) {
    const char *base = getenv("RETRO_STUDY_NOISE_DIR");
    const char *suffix = "";
    if (!base || !*base) {
#ifdef __APPLE__
        base = getenv("HOME");
        suffix = "/Library/Application Support/RetroStudy/Noise";
#else
        base = getenv("XDG_DATA_HOME");
        suffix = "/RetroStudy/Noise";
        if (!base || !*base) {
            base = getenv("HOME");
            suffix = "/.local/share/RetroStudy/Noise";
        }
#endif
    }
    if (!base || !*base)
        return NULL;

    const char *format = "%s%s/990101_%03u_00_v700_Noise [ABC].nxac";
    int length = snprintf(NULL, 0, format, base, suffix, id);
    if (length < 0)
        return NULL;
    size_t size = (size_t)length + 1;
    char *path = malloc(size);
    if (!path)
        return NULL;
    snprintf(path, size, format, base, suffix, id);
    FILE *file = fopen(path, "rb");
    free(path);
    return file;
}

static texture load_texture(unsigned id) {
    FILE *file = open_texture(id);
    if (!file)
        return (texture){0};

    /* The largest supplied texture has 884,928 stereo frames. Bound memory
       use when the user directory contains an unrelated or malformed file. */
    enum { MAX_FRAMES = 1000000, MAX_FILE_SIZE = 4 * 1024 * 1024 };
    unsigned char *encoded = NULL;
    size_t size = 0;
    if (!fseek(file, 0, SEEK_END)) {
        long length = ftell(file);
        if (length > 0 && length <= MAX_FILE_SIZE && !fseek(file, 0, SEEK_SET)) {
            size = (size_t)length;
            encoded = malloc(size);
            if (encoded && (fread(encoded, 1, size, file) != size || fgetc(file) != EOF)) {
                free(encoded);
                encoded = NULL;
            }
        }
    }
    fclose(file);

    rs_nxac sample;
    texture result = {0};
    if (encoded && rs_nxac_open(&sample, encoded, size) && sample.frames <= MAX_FRAMES) {
        float *pcm = malloc((size_t)sample.frames * 2 * sizeof(*pcm));
        if (pcm && rs_nxac_decode_pcm(&sample, pcm, pcm + sample.frames, sample.frames))
            result = (texture){.pcm = pcm, .frames = sample.frames, .rate = sample.sample_rate};
        else
            free(pcm);
    }
    free(encoded);
    return result;
}

const rs_noise_assets *rs_noise_assets_acquire(void) {
    pthread_mutex_lock(&bank_lock);
    if (references == 0)
        for (unsigned i = 0; i < RS_NOISE_TYPES; ++i)
            bank.clips[i] = load_texture(textures[i].id);
    ++references;
    pthread_mutex_unlock(&bank_lock);
    return &bank;
}

void rs_noise_assets_release(const rs_noise_assets *assets) {
    if (!assets)
        return;
    pthread_mutex_lock(&bank_lock);
    if (--references == 0) {
        for (unsigned i = 0; i < RS_NOISE_TYPES; ++i)
            free(bank.clips[i].pcm);
        memset(&bank, 0, sizeof(bank));
    }
    pthread_mutex_unlock(&bank_lock);
}

bool rs_noise_assets_prepare(const rs_noise_assets *assets, unsigned type, double rate,
                             rs_noise_player *player) {
    if (!assets || type >= RS_NOISE_TYPES || !assets->clips[type].pcm)
        return false;
    const texture *clip = &assets->clips[type];
    return rs_noise_player_prepare(player, clip->pcm, clip->pcm + clip->frames,
                                   clip->frames, clip->rate, rate);
}
