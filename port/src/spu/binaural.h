#ifndef PORT_SPU_BINAURAL_H
#define PORT_SPU_BINAURAL_H

#include <stdint.h>

/* Device-side history in SPU samples. Duplicating the ring gives each FIR a
 * contiguous window even when its oldest and newest samples straddle zero. */
enum { BINAURAL_TAPS = 128, BINAURAL_FADE = 256 };
typedef struct {
    int32_t front[BINAURAL_TAPS * 2];
    int32_t sum[BINAURAL_TAPS * 2];
    int32_t difference[BINAURAL_TAPS * 2];
    unsigned cursor;
    int blend;
    int enabled; /* direction of the saved crossfade */
} SpuHeadphones;

void spuBinauralMix(SpuHeadphones* h, int enabled, int32_t dry[2], int32_t center);
int spuRunHeadphones(int16_t* out, uint32_t frames, SpuHeadphones* h, int enabled);

#endif
