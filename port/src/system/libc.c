/*
 * PsyQ LIBC2 rand/srand: the game's random sequences depend on PsyQ's own
 * generator (src/main/psyq/libc2/rand.c), not the host's: a 32-bit LCG,
 * x = x * 0x41C64E6D + 12345, returning bits 16..30. The seed starts at 0
 * (it lives in .bss) and the game never reseeds it.
 *
 * Hidden: the game's calls bind to these at link time, but shared libraries
 * (SDL, the Vulkan driver) keep the C library's rand, so they cannot advance
 * the game's sequence.
 */

#include "common.h"
#include "savestate.h"

#ifdef _WIN32
#define XG_HIDDEN /* DLLs bind to the C runtime's rand by import */
#else
#define XG_HIDDEN __attribute__((visibility("hidden")))
#endif

static u32 seed;

XG_HIDDEN s32 rand(void) {
    seed = seed * 0x41C64E6Du + 12345u;
    return (s32)((seed >> 16) & 0x7FFF);
}

XG_HIDDEN void srand(u32 newSeed) {
    seed = newSeed;
}

/* The seed, for state logs (XG_SWEEP_LOG); the PS1's is randSeed (US). */
u32 portRandSeed(void) {
    return seed;
}

/* Save states (savestate.h): a load replays the same random outcomes. */
void libcStateIo(XgState* s) {
    XG_STATE_VAR(s, seed);
}
