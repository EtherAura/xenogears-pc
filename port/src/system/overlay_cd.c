/*
 * Overlay CD reads, an option for validation runs (XG_OVERLAY_CD).
 *
 * Every overlay is linked into the PC executable, so where the PS1 reads an
 * overlay file from the disc (bootGame's game-mode overlays, the menus, the
 * battle sub-overlays, the movie mode's video player, field_mecha) the port
 * only puts the overlay's data back as a fresh load leaves it
 * (portResetModule), and menus and modes open at once. With this option the
 * game code also makes the PS1's reads, with the same CD calls, into a
 * scratch buffer that is then discarded (bootGame decompresses its overlay
 * too, and makes the PS1's VSync calls after it), so the CD takes the PS1's
 * time and a validation run lines up with PCSX-Redux frame for frame.
 *
 * Where the PS1 reads into a heap block of the file's size (bootGame's
 * compressed overlay, field_mecha and the menus in mechaOverlayLoadingMode 1)
 * the PC reads into that same block. Where it reads to the overlay's link
 * address, padded down to with heap blocks, the PC keeps its token blocks and
 * reads into scratch memory outside the game heap, so the heap is the same
 * with and without the option.
 *
 *   XG_OVERLAY_CD=1  always on; 0 off. Unset: on while the slowdown replay
 *                    (XG_SLOWDOWN, slowdown.c) is lined up with its log, so a
 *                    PC pad script made for the instant loads still reaches
 *                    the log's first mode start.
 */

#include <stdio.h>
#include <stdlib.h>

#include "system.h"

int32_t portOverlayCd(void) {
    static int forced = -2; /* -2: not read yet, -1: follow the slowdown replay */
    static int told;
    int64_t vblank;
    int on;

    if (forced == -2) {
        const char* s = getenv("XG_OVERLAY_CD");
        forced = s != NULL && s[0] != '\0' ? atoi(s) != 0 : -1;
    }
    on = forced >= 0 ? forced : slowdownLogVBlank(&vblank);
    if (on && !told) {
        told = 1;
        fprintf(stderr, "overlay cd: overlay files are read as the PS1 reads them (%s)\n",
                forced == 1 ? "XG_OVERLAY_CD=1" : "with the slowdown replay; XG_OVERLAY_CD=0 turns it off");
    }
    return on;
}

#define SCRATCH_SLOTS 3
#define SCRATCH_MIN (1024 * 1024)
#define SCRATCH_SLACK 0x800 /* the LZSS decoder may finish a group past the end */

/* A buffer of at least `size` bytes for slot `slot` (0: an overlay file,
 * 1: a second file of the same batch, 2: a decompressed overlay). A buffer is
 * never freed: a read into it may still be in flight when the slot grows. */
void* portOverlayScratch(int32_t slot, int32_t size) {
    static unsigned char* buf[SCRATCH_SLOTS];
    static size_t cap[SCRATCH_SLOTS];
    size_t want;

    if (slot < 0 || slot >= SCRATCH_SLOTS || size < 0) {
        return NULL;
    }
    want = (size_t)size + SCRATCH_SLACK;
    if (want > cap[slot]) {
        size_t n = want > SCRATCH_MIN ? want : SCRATCH_MIN;
        unsigned char* p = malloc(n);
        if (p == NULL) {
            return NULL;
        }
        buf[slot] = p;
        cap[slot] = n;
    }
    return buf[slot];
}
