#ifndef RETRO_SPACE_INTERNAL_H
#define RETRO_SPACE_INTERNAL_H
#include "space.h"
#include <stdint.h>
#include <string.h>
/* Mirror only numerical state/cache layout; no native C++ objects execute in C. */
typedef union {
    _Alignas(16) unsigned char bytes[0x1160];
    uint32_t words[0x1160/4];
    float values[0x1160/4];
} rs_space_native;
struct retro_space {
    rs_space_native native;
    retro_space_params p;
    float rate;
    unsigned remaining;
    float batch[256];
    int32_t batch_int[32];
};
#define SPACE_F(s,o) ((s)->native.values[(o)/4])
#define SPACE_U(s,o) ((s)->native.words[(o)/4])
#define SPACE_B(s,o) ((s)->native.bytes[(o)])
static inline void *rs_space_pointer(const retro_space *s,size_t offset) {
    void *p;memcpy(&p,s->native.bytes+offset,sizeof(p));return p;
}
static inline void rs_space_store_pointer(retro_space *s,size_t offset,void *p) {
    memcpy(s->native.bytes+offset,&p,sizeof(p));
}
/* Allocator/prepare owner supplies these; public lifecycle belongs to space.c. */
int rs_space_prepare_state(retro_space *s,float rate);
void rs_space_free_state(retro_space *s);
void rs_space_clear_state(retro_space *s);
void rs_space_rewind_state(retro_space *s);
#endif
